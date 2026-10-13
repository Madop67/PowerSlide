#!/bin/bash
# CTR Native Build Script (macOS, Apple Silicon)
# Builds the game as a wasm32 sandbox translated to native C; see cmake/ctr_wasm_sandbox.cmake.

set -e

if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake not found"
    echo "Install: brew install cmake"
    exit 1
fi

./tools/macos/fetch-toolchain.sh

cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
ctest --test-dir build-macos --output-on-failure

echo ""
echo "Build succeeded: build-macos/ctr_native"
