#!/bin/bash
# CTR Native Build Script (Linux x86_64 release)
# Builds the game as a wasm32 sandbox translated to native C (see
# cmake/ctr_wasm_sandbox.cmake), so the result is a 64-bit binary that needs
# no 32-bit libraries. Packages it as a tarball (skip with --no-package).
#
# The native 32-bit build for development, matching and replays is ./build.sh.
#
# Requires: cmake, gcc or clang, python3, curl, and the 64-bit SDL3 build
# dependencies, e.g. on Debian/Ubuntu:
#   sudo apt install cmake build-essential python3 curl libx11-dev libxext-dev \
#     libxcursor-dev libxi-dev libxrandr-dev libxfixes-dev libxkbcommon-dev \
#     libwayland-dev libegl1-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev \
#     libasound2-dev libpulse-dev libudev-dev libdbus-1-dev

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
    echo "Install: sudo apt install cmake"
    exit 1
fi

./tools/wasm/fetch-toolchain.sh

cmake -S . -B build-linux-x64 -DCMAKE_BUILD_TYPE=Release -DCTR_NATIVE_SANDBOX=ON -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-linux-x64 -j"$(nproc)"
ctest --test-dir build-linux-x64 --output-on-failure

if [[ "${MAKE_PACKAGE}" == 1 ]]; then
    ./package-linux.sh
fi

echo ""
echo "Build succeeded: build-linux-x64/ctr_native"
