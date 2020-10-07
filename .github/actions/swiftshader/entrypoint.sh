#!/bin/bash
set -e

function print_help {
    local self_name=$(basename "$0")
    echo "This script issues commands for testing Filament with SwiftShader."
    echo ""
    echo "Usage:"
    echo "    $self_name [command]"
    echo ""
    echo "Commands:"
    echo "    build filament"
    echo "    build swiftshader"
    echo "    help"
    echo "    test"
    echo ""
}

BUILD_TYPE="release"

if [[ "$1" == "build" ]] && [[ "$2" == "filament" ]]; then
    filament/build.sh -t ${BUILD_TYPE} gltf_viewer
    exit $?
fi

if [[ "$1" == "build" ]] && [[ "$2" == "swiftshader" ]]; then
    BUILD_TYPE="$(tr '[:lower:]' '[:upper:]' <<< ${BUILD_TYPE:0:1})${BUILD_TYPE:1}"
    cd swiftshader
    rm -rf build
    mkdir build
    cd build
    cmake -GNinja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" ..
    ninja
    exit $?
fi

if [[ "$1" == "help" ]]; then
    print_help
    exit 0
fi

if [[ "$1" == "test" ]]; then
    mkdir -p results
    cd results
    ../filament/out/cmake-release/samples/gltf_viewer \
          --headless \
          --batch ../libs/viewer/tests/basic.json \
          --api vulkan
    ../filament/build/swiftshader/gallery.py
    exit $?
fi

print_help
exit 1
