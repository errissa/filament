/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//#define FILAMENT_USE_EXTERNAL_GLES3 1

#include "PlatformEGLHeadless.h"

#include "OpenGLDriver.h"
#include "OpenGLContext.h"
#include "OpenGLDriverFactory.h"
#include "utils/Panic.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <utils/compiler.h>
#include <utils/Log.h>

#include <assert.h>


extern "C" filament::backend::Platform* CreateEGLHeadlessPlatform()
{return new filament::PlatformEGLHeadless();}

using namespace utils;

namespace filament {
using namespace backend;

// The Android NDK doesn't exposes extensions, fake it with eglGetProcAddress
namespace glext {
UTILS_PRIVATE PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR = {};
UTILS_PRIVATE PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR = {};
UTILS_PRIVATE PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR = {};
UTILS_PRIVATE PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = {};
UTILS_PRIVATE PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = {};
}
using namespace glext;

// ---------------------------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------------------------

void PlatformEGLHeadless::logEglError(const char* name) noexcept {
    const char* err;
    switch (eglGetError()) {
        case EGL_NOT_INITIALIZED:       err = "EGL_NOT_INITIALIZED";    break;
        case EGL_BAD_ACCESS:            err = "EGL_BAD_ACCESS";         break;
        case EGL_BAD_ALLOC:             err = "EGL_BAD_ALLOC";          break;
        case EGL_BAD_ATTRIBUTE:         err = "EGL_BAD_ATTRIBUTE";      break;
        case EGL_BAD_CONTEXT:           err = "EGL_BAD_CONTEXT";        break;
        case EGL_BAD_CONFIG:            err = "EGL_BAD_CONFIG";         break;
        case EGL_BAD_CURRENT_SURFACE:   err = "EGL_BAD_CURRENT_SURFACE";break;
        case EGL_BAD_DISPLAY:           err = "EGL_BAD_DISPLAY";        break;
        case EGL_BAD_SURFACE:           err = "EGL_BAD_SURFACE";        break;
        case EGL_BAD_MATCH:             err = "EGL_BAD_MATCH";          break;
        case EGL_BAD_PARAMETER:         err = "EGL_BAD_PARAMETER";      break;
        case EGL_BAD_NATIVE_PIXMAP:     err = "EGL_BAD_NATIVE_PIXMAP";  break;
        case EGL_BAD_NATIVE_WINDOW:     err = "EGL_BAD_NATIVE_WINDOW";  break;
        case EGL_CONTEXT_LOST:          err = "EGL_CONTEXT_LOST";       break;
        default:                        err = "unknown";                break;
    }
    slog.e << name << " failed with " << err << io::endl;
}

// ---------------------------------------------------------------------------------------------

PlatformEGLHeadless::PlatformEGLHeadless() noexcept = default;

Driver* PlatformEGLHeadless::createDriver(void* sharedContext) noexcept {
    mEGLDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(mEGLDisplay != EGL_NO_DISPLAY);

    int retval;
    EGLint major, minor;
    EGLBoolean initialized = eglInitialize(mEGLDisplay, &major, &minor);
    if (UTILS_UNLIKELY(!initialized)) {
        slog.e << "eglInitialize failed" << io::endl;
        return nullptr;
    }

    slog.w << "EGL(" << major << "." << minor << ")" << io::endl;
    
    auto extensions = GLUtils::split(eglQueryString(mEGLDisplay, EGL_EXTENSIONS));

    eglCreateSyncKHR = (PFNEGLCREATESYNCKHRPROC) eglGetProcAddress("eglCreateSyncKHR");
    eglDestroySyncKHR = (PFNEGLDESTROYSYNCKHRPROC) eglGetProcAddress("eglDestroySyncKHR");
    eglClientWaitSyncKHR = (PFNEGLCLIENTWAITSYNCKHRPROC) eglGetProcAddress("eglClientWaitSyncKHR");

    eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");

    // Config suitable for OpenGL and offscreen rendering
    EGLint configsCount;
    EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RED_SIZE,    8,
            EGL_GREEN_SIZE,  8,
            EGL_BLUE_SIZE,   8,
            EGL_ALPHA_SIZE,  8,
            EGL_DEPTH_SIZE, 24,
            EGL_NONE
    };

    EGLint contextAttribs[] = {
        // Filament requires OpenGL 4.1+
        EGL_CONTEXT_MAJOR_VERSION, 4,
        EGL_CONTEXT_MINOR_VERSION, 1,
        EGL_NONE, EGL_NONE,
        EGL_NONE
    };

    EGLint pbufferAttribs[] = {
            EGL_WIDTH,  1,
            EGL_HEIGHT, 1,
            EGL_NONE
    };

#ifdef NDEBUG
    // When we don't have a shared context and we're in release mode, we always activate the
    // EGL_KHR_create_context_no_error extension.
    if (!sharedContext && extensions.has("EGL_KHR_create_context_no_error")) {
        contextAttribs[4] = EGL_CONTEXT_OPENGL_NO_ERROR_KHR;
        contextAttribs[5] = EGL_TRUE;
    }
#endif

    // Get config
    if (!eglChooseConfig(mEGLDisplay, configAttribs, &mEGLConfig, 1, &configsCount)) {
        logEglError("eglChooseConfig");
        goto error;
    }

    if (configsCount == 0) {
        slog.e << "Failed to find any suitable EGL configs" << io::endl;
        goto error;
    }

    mEGLDummySurface = eglCreatePbufferSurface(mEGLDisplay, mEGLConfig, pbufferAttribs);
    if (mEGLDummySurface == EGL_NO_SURFACE) {
        logEglError("eglCreatePbufferSurface");
        goto error;
    }

    // EGL Headless uses OpenGL API
    if(!eglBindAPI(EGL_OPENGL_API)) {
        logEglError("eglBindAPI");
    }
    
    mEGLContext = eglCreateContext(mEGLDisplay, mEGLConfig, (EGLContext)sharedContext, contextAttribs);
    if (UTILS_UNLIKELY(mEGLContext == EGL_NO_CONTEXT)) {
        // eglCreateContext failed
        logEglError("eglCreateContext");
        goto error;
    }

    if (!makeCurrent(mEGLDummySurface, mEGLDummySurface)) {
        // eglMakeCurrent failed
        logEglError("eglMakeCurrent");
        goto error;
    }

    // Bind OpenGL API
    retval = bluegl::bind();
    ASSERT_POSTCONDITION(!retval, "Unable to load OpenGL entry points.");
    
    // NOTE: holdover from Platform EGL. I don't think it's necessary
    initializeGlExtensions();

    // success!!
    return OpenGLDriverFactory::create(this, sharedContext);

error:
    // if we're here, we've failed
    if (mEGLDummySurface) {
        eglDestroySurface(mEGLDisplay, mEGLDummySurface);
    }
    if (mEGLContext) {
        eglDestroyContext(mEGLDisplay, mEGLContext);
    }

    mEGLDummySurface = EGL_NO_SURFACE;
    mEGLContext = EGL_NO_CONTEXT;

    eglTerminate(mEGLDisplay);
    eglReleaseThread();

    return nullptr;
}

EGLBoolean PlatformEGLHeadless::makeCurrent(EGLSurface drawSurface, EGLSurface readSurface) noexcept {
    if (UTILS_UNLIKELY((drawSurface != mCurrentDrawSurface || readSurface != mCurrentReadSurface))) {
        mCurrentDrawSurface = drawSurface;
        mCurrentReadSurface = readSurface;
        return eglMakeCurrent(mEGLDisplay, drawSurface, readSurface, mEGLContext);
    }
    return EGL_TRUE;
}

void PlatformEGLHeadless::terminate() noexcept {
    eglMakeCurrent(mEGLDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(mEGLDisplay, mEGLDummySurface);
    eglDestroyContext(mEGLDisplay, mEGLContext);
    eglTerminate(mEGLDisplay);
    eglReleaseThread();
}

Platform::SwapChain* PlatformEGLHeadless::createSwapChain(
        void* nativeWindow, uint64_t& flags) noexcept {
    // NOTE: ignoring backend::SWAP_CHAIN_CONFIG_TRANSPARENT flag and always
    // returning a surface based on same config
    EGLSurface sur = eglCreateWindowSurface(mEGLDisplay, mEGLConfig,
            (EGLNativeWindowType)nativeWindow, nullptr);

    if (UTILS_UNLIKELY(sur == EGL_NO_SURFACE)) {
        logEglError("eglCreateWindowSurface");
        return nullptr;
    }
    if (!eglSurfaceAttrib(mEGLDisplay, sur, EGL_SWAP_BEHAVIOR, EGL_BUFFER_DESTROYED)) {
        logEglError("eglSurfaceAttrib(..., EGL_SWAP_BEHAVIOR, EGL_BUFFER_DESTROYED)");
        // this is not fatal
    }
    return (SwapChain*)sur;
}

Platform::SwapChain* PlatformEGLHeadless::createSwapChain(
        uint32_t width, uint32_t height, uint64_t& flags) noexcept {

    EGLint attribs[] = {
            EGL_WIDTH, EGLint(width),
            EGL_HEIGHT, EGLint(height),
            EGL_NONE
    };

    // NOTE: see note above regarding flags
    EGLSurface sur = eglCreatePbufferSurface(mEGLDisplay, mEGLConfig, attribs);

    if (UTILS_UNLIKELY(sur == EGL_NO_SURFACE)) {
        logEglError("eglCreatePbufferSurface");
        return nullptr;
    }
    return (SwapChain*)sur;
}

void PlatformEGLHeadless::destroySwapChain(Platform::SwapChain* swapChain) noexcept {
    EGLSurface sur = (EGLSurface) swapChain;
    if (sur != EGL_NO_SURFACE) {
        makeCurrent(mEGLDummySurface, mEGLDummySurface);
        eglDestroySurface(mEGLDisplay, sur);
    }
}

void PlatformEGLHeadless::makeCurrent(Platform::SwapChain* drawSwapChain,
                              Platform::SwapChain* readSwapChain) noexcept {
    EGLSurface drawSur = (EGLSurface) drawSwapChain;
    EGLSurface readSur = (EGLSurface) readSwapChain;
    if (drawSur != EGL_NO_SURFACE || readSur != EGL_NO_SURFACE) {
        makeCurrent(drawSur, readSur);
    }
}

void PlatformEGLHeadless::commit(Platform::SwapChain* swapChain) noexcept {
    EGLSurface sur = (EGLSurface) swapChain;
    if (sur != EGL_NO_SURFACE) {
        eglSwapBuffers(mEGLDisplay, sur);
    }
}

Platform::Fence* PlatformEGLHeadless::createFence() noexcept {
    Fence* f = nullptr;
#ifdef EGL_KHR_reusable_sync
    f = (Fence*) eglCreateSyncKHR(mEGLDisplay, EGL_SYNC_FENCE_KHR, nullptr);
#endif
    return f;
}

void PlatformEGLHeadless::destroyFence(Platform::Fence* fence) noexcept {
#ifdef EGL_KHR_reusable_sync
    EGLSyncKHR sync = (EGLSyncKHR) fence;
    if (sync != EGL_NO_SYNC_KHR) {
        eglDestroySyncKHR(mEGLDisplay, sync);
    }
#endif
}

backend::FenceStatus PlatformEGLHeadless::waitFence(
        Platform::Fence* fence, uint64_t timeout) noexcept {
#ifdef EGL_KHR_reusable_sync
    EGLSyncKHR sync = (EGLSyncKHR) fence;
    if (sync != EGL_NO_SYNC_KHR) {
        EGLint status = eglClientWaitSyncKHR(mEGLDisplay, sync, 0, (EGLTimeKHR)timeout);
        if (status == EGL_CONDITION_SATISFIED_KHR) {
            return FenceStatus::CONDITION_SATISFIED;
        }
        if (status == EGL_TIMEOUT_EXPIRED_KHR) {
            return FenceStatus::TIMEOUT_EXPIRED;
        }
    }
#endif
    return FenceStatus::ERROR;
}

void PlatformEGLHeadless::createExternalImageTexture(void* texture) noexcept {
    auto* t = (OpenGLDriver::GLTexture*) texture;
    glGenTextures(1, &t->gl.id);
    t->gl.target = GL_TEXTURE_2D;
    t->gl.targetIndex = (uint8_t)
                OpenGLContext::getIndexForTextureTarget(GL_TEXTURE_2D);
}

void PlatformEGLHeadless::destroyExternalImage(void* texture) noexcept {
    auto* t = (OpenGLDriver::GLTexture*) texture;
    glDeleteTextures(1, &t->gl.id);
}

void PlatformEGLHeadless::initializeGlExtensions() noexcept {
    // NOTE: I'm not sure what this does. Maybe just a sanity check for
    // debugging purposes?
    GLUtils::unordered_string_set glExtensions;
    GLint n;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    slog.w << "PlatformEGLHeadless: Found " << n << " OpenGL extensions" << io::endl;
    
    for (GLint i = 0; i < n; ++i) {
        const char * const extension = (const char*) glGetStringi(GL_EXTENSIONS, (GLuint)i);
        glExtensions.insert(extension);
    }
}

} // namespace filament

// ---------------------------------------------------------------------------------------------
