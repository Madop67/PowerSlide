#!/bin/bash
# CTR Native Build Script (macOS, Apple Silicon)
# Builds the game as a wasm32 sandbox translated to native C; see cmake/ctr_wasm_sandbox.cmake.
# Also assembles PowerSlide.app and packages it as a DMG (skip with --no-dmg).

set -e

MAKE_DMG=1
for arg in "$@"; do
    case "${arg}" in
        --no-dmg) MAKE_DMG=0 ;;
        *)
            echo "Usage: $0 [--no-dmg]" >&2
            exit 1
            ;;
    esac
done

if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake not found"
    echo "Install: brew install cmake"
    exit 1
fi

./tools/wasm/fetch-toolchain.sh

cmake -S . -B build-macos -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
ctest --test-dir build-macos --output-on-failure

if [[ "${MAKE_DMG}" == 1 ]]; then
    ./package-macos.sh
fi

echo ""
echo "Build succeeded: build-macos/ctr_native"
echo "App bundle:      build-macos/PowerSlide.app"
