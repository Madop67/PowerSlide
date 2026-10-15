#!/bin/bash
# CTR Native Build Script (Android)
# Builds the game as a wasm32 sandbox translated to native C (see
# cmake/ctr_wasm_sandbox.cmake) into libmain.so, wraps it in an APK with SDL's
# SDLActivity (android/), and packages it (skip with --no-package).
#
# Needs the Android SDK (ANDROID_HOME, or ~/Library/Android/sdk / ~/Android/Sdk)
# with an NDK and CMake installed, and a JDK 17 or 21 (JAVA_HOME; Android
# Studio's bundled one is found on its own).

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

if [[ -z "${ANDROID_HOME:-}" ]]; then
    for candidate in "${ANDROID_SDK_ROOT:-}" "${HOME}/Library/Android/sdk" "${HOME}/Android/Sdk"; do
        if [[ -n "${candidate}" && -d "${candidate}" ]]; then
            export ANDROID_HOME="${candidate}"
            break
        fi
    done
fi
if [[ -z "${ANDROID_HOME:-}" ]]; then
    echo "ERROR: Android SDK not found; set ANDROID_HOME" >&2
    exit 1
fi

if [[ -z "${JAVA_HOME:-}" ]]; then
    for candidate in "/Applications/Android Studio.app/Contents/jbr/Contents/Home" "/opt/android-studio/jbr"; do
        if [[ -x "${candidate}/bin/java" ]]; then
            export JAVA_HOME="${candidate}"
            break
        fi
    done
fi

# Newest installed NDK and SDK CMake, unless ANDROID_NDK_VERSION / ANDROID_CMAKE_VERSION say otherwise.
NDK_VERSION="${ANDROID_NDK_VERSION:-$(ls "${ANDROID_HOME}/ndk" 2>/dev/null | sort -V | tail -n 1)}"
CMAKE_VERSION="${ANDROID_CMAKE_VERSION:-$(ls "${ANDROID_HOME}/cmake" 2>/dev/null | sort -V | tail -n 1)}"
if [[ -z "${NDK_VERSION}" || -z "${CMAKE_VERSION}" ]]; then
    echo "ERROR: install an NDK and CMake with the Android SDK manager" >&2
    echo "  sdkmanager 'ndk;28.2.13676358' 'cmake;3.31.6'" >&2
    exit 1
fi
echo "Android SDK: ${ANDROID_HOME} (NDK ${NDK_VERSION}, CMake ${CMAKE_VERSION})"

./tools/wasm/fetch-toolchain.sh

(
    cd android
    ./gradlew --no-daemon assembleRelease -PctrNdkVersion="${NDK_VERSION}" -PctrCmakeVersion="${CMAKE_VERSION}"
)

APK="android/app/build/outputs/apk/release/app-release.apk"
python3 tools/release/check_standalone.py "${APK}"

if [[ "${MAKE_PACKAGE}" == 1 ]]; then
    ./package-android.sh
fi

echo ""
echo "Build succeeded: ${APK}"
