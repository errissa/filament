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

#include "SpirvDictionaryReader.h"

#if defined (FILAMENT_DRIVER_SUPPORTS_VULKAN)
#include <utils/Log.h>
#include <smolv.h>
#endif

#include <assert.h>

namespace filaflat {

bool SpirvDictionaryReader::unflatten(Unflattener& f, BlobDictionary& dictionary) {
    uint32_t compressionScheme;
    if (!f.read(&compressionScheme)) {
        return false;
    }

    // For now, 1 is the only acceptable compression scheme.
    assert(compressionScheme == 1);

    uint32_t numBlobs;
    if (!f.read(&numBlobs)) {
        return false;
    }

    dictionary.reserve(numBlobs);
    for (uint32_t i = 0; i < numBlobs; i++) {
        const char* compressed;
        size_t compressedSize;
        if (!f.read(&compressed, &compressedSize)) {
            return false;
        }

#if defined (FILAMENT_DRIVER_SUPPORTS_VULKAN)
        size_t spirvSize = smolv::GetDecodedBufferSize(compressed, compressedSize);
        if (spirvSize == 0) {
            utils::slog.e << "Error with SPIRV decompression" << utils::io::endl;
            return false;
        }
        BlobDictionary::Blob spirv(spirvSize);
        if (!smolv::Decode(compressed, compressedSize, spirv.data(), spirvSize)) {
            utils::slog.e << "Error with SPIRV decompression" << utils::io::endl;
            return false;
        }
        dictionary.addBlob(std::move(spirv));
#else
        return false;
#endif

    }
    return true;
}

} // namespace filaflat
