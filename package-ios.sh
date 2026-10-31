#!/usr/bin/env bash
# Package build-ios/PowerSlide.app as an unsigned .ipa for sideloading.
#
#   ./package-ios.sh [version]
#
# The .ipa is not signed: AltStore, SideStore or Sideloadly sign it with the
# player's own Apple ID when installing it.

set -euo pipefail

cd "$(dirname "$0")"

VERSION="${1:-$(sed -n 's/^set(CTR_NATIVE_VERSION "\(.*\)")$/\1/p' CMakeLists.txt)}"
BUILD_DIR="${BUILD_DIR:-build-ios}"
DIST_DIR="${DIST_DIR:-dist}"
APP_NAME="PowerSlide"
APP_PATH="${BUILD_DIR}/${APP_NAME}.app"
PACKAGE_NAME="${APP_NAME}-${VERSION}-ios"
STAGE_DIR="${DIST_DIR}/${PACKAGE_NAME}"
IPA_PATH="${DIST_DIR}/${PACKAGE_NAME}.ipa"

if [[ ! -x "${APP_PATH}/ctr_native" ]]; then
    echo "ERROR: missing app bundle: ${APP_PATH}" >&2
    echo "Run ./build-ios.sh first." >&2
    exit 1
fi

# The app must stand alone: SDL3 and libchdr are static, so only system
# libraries and frameworks may appear here.
foreign_libs="$(otool -L "${APP_PATH}/ctr_native" | tail -n +2 | awk '{print $1}' | grep -Ev '^(/System/|/usr/lib/)' || true)"
if [[ -n "${foreign_libs}" ]]; then
    echo "ERROR: ctr_native links non-system libraries:" >&2
    echo "${foreign_libs}" >&2
    exit 1
fi

rm -rf "${STAGE_DIR}" "${IPA_PATH}"
mkdir -p "${STAGE_DIR}/Payload"
ditto "${APP_PATH}" "${STAGE_DIR}/Payload/${APP_NAME}.app"

(
    cd "${STAGE_DIR}"
    zip -qry -X "../${PACKAGE_NAME}.ipa" Payload
)
rm -rf "${STAGE_DIR}"

(
    cd "${DIST_DIR}"
    shasum -a 256 "${PACKAGE_NAME}.ipa" >"${PACKAGE_NAME}.ipa.sha256"
)

echo "Wrote ${IPA_PATH}"
echo "Wrote ${IPA_PATH}.sha256"
