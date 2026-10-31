#!/bin/bash
# CTR Native Build Script (iOS / iPadOS)
# Builds the game as a wasm32 sandbox translated to native C (see
# cmake/ctr_wasm_sandbox.cmake), assembles an unsigned build-ios/PowerSlide.app
# and packages it as an .ipa for sideloading (skip with --no-package).
#
# Needs a Mac with Xcode (the iOS SDK) and CMake. IOS_DEPLOYMENT_TARGET sets the
# oldest iOS version (default 15.0).

set -e

cd "$(dirname "$0")"

MAKE_PACKAGE=1
for arg in "$@"; do
    case "${arg}" in
        --no-package) MAKE_PACKAGE=0 ;;
        *)
            echo "Usage: $0 [--no-package]" >&2
            exit 1
            ;;
    esac
done

if ! command -v cmake &>/dev/null; then
    echo "ERROR: cmake not found"
    echo "Install: brew install cmake"
    exit 1
fi

if ! xcrun --sdk iphoneos --show-sdk-path &>/dev/null; then
    echo "ERROR: the iOS SDK was not found" >&2
    echo "Install Xcode, then: sudo xcode-select -s /Applications/Xcode.app" >&2
    exit 1
fi

./tools/wasm/fetch-toolchain.sh

cmake -S . -B build-ios \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${IOS_DEPLOYMENT_TARGET:-15.0}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
cmake --build build-ios -j"$(sysctl -n hw.ncpu)"

if [[ "${MAKE_PACKAGE}" == 1 ]]; then
    ./package-ios.sh
fi

echo ""
echo "Build succeeded: build-ios/PowerSlide.app"
