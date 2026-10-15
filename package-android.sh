#!/usr/bin/env bash
# Package the Android APK for release.
#
#   ./package-android.sh [version]
#
# The APK is signed by Gradle: with the release key when ANDROID_KEYSTORE,
# ANDROID_KEYSTORE_PASSWORD, ANDROID_KEY_ALIAS and ANDROID_KEY_PASSWORD are
# set for ./build-android.sh, otherwise with the debug key (fine to sideload).

set -euo pipefail

cd "$(dirname "$0")"

VERSION="${1:-$(sed -n 's/^set(CTR_NATIVE_VERSION "\(.*\)")$/\1/p' CMakeLists.txt)}"
DIST_DIR="${DIST_DIR:-dist}"
APK="${APK:-android/app/build/outputs/apk/release/app-release.apk}"
PACKAGE_NAME="PowerSlide-${VERSION}-android"

if [[ ! -f "${APK}" ]]; then
    echo "ERROR: missing APK: ${APK}" >&2
    echo "Run ./build-android.sh first." >&2
    exit 1
fi

python3 tools/release/check_standalone.py "${APK}"

mkdir -p "${DIST_DIR}"
cp "${APK}" "${DIST_DIR}/${PACKAGE_NAME}.apk"

(
    cd "${DIST_DIR}"
    if command -v sha256sum &>/dev/null; then
        sha256sum "${PACKAGE_NAME}.apk" >"${PACKAGE_NAME}.apk.sha256"
    else
        shasum -a 256 "${PACKAGE_NAME}.apk" >"${PACKAGE_NAME}.apk.sha256"
    fi
)

echo "Wrote ${DIST_DIR}/${PACKAGE_NAME}.apk"
echo "Wrote ${DIST_DIR}/${PACKAGE_NAME}.apk.sha256"
