#!/usr/bin/env bash
# Package build-macos/PowerSlide.app as a DMG for release.
#
#   ./package-macos.sh [version]
#
# Signing is ad-hoc unless CODESIGN_IDENTITY is set (a "Developer ID
# Application: ..." identity). With NOTARY_PROFILE also set (a notarytool
# keychain profile), the DMG is notarized and stapled.

set -euo pipefail

cd "$(dirname "$0")"

VERSION="${1:-$(sed -n 's/^set(CTR_NATIVE_VERSION "\(.*\)")$/\1/p' CMakeLists.txt)}"
BUILD_DIR="${BUILD_DIR:-build-macos}"
DIST_DIR="${DIST_DIR:-dist}"
APP_NAME="PowerSlide"
APP_PATH="${BUILD_DIR}/${APP_NAME}.app"
PACKAGE_NAME="${APP_NAME}-${VERSION}-macos-arm64"
STAGE_DIR="${DIST_DIR}/${PACKAGE_NAME}"
DMG_PATH="${DIST_DIR}/${PACKAGE_NAME}.dmg"

if [[ ! -x "${APP_PATH}/Contents/MacOS/ctr_native" ]]; then
    echo "ERROR: missing app bundle: ${APP_PATH}" >&2
    echo "Run ./build-macos.sh first." >&2
    exit 1
fi

# The app must stand alone: SDL3 and libchdr are static, so only system
# libraries may appear here.
foreign_libs="$(otool -L "${APP_PATH}/Contents/MacOS/ctr_native" | tail -n +2 | awk '{print $1}' | grep -Ev '^(/System/|/usr/lib/)' || true)"
if [[ -n "${foreign_libs}" ]]; then
    echo "ERROR: ctr_native links non-system libraries:" >&2
    echo "${foreign_libs}" >&2
    exit 1
fi

rm -rf "${STAGE_DIR}" "${DMG_PATH}"
mkdir -p "${STAGE_DIR}"

# ditto keeps the bundle's signature and extended attributes intact.
ditto "${APP_PATH}" "${STAGE_DIR}/${APP_NAME}.app"
ln -s /Applications "${STAGE_DIR}/Applications"

if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
    codesign --force --options runtime --timestamp --sign "${CODESIGN_IDENTITY}" "${STAGE_DIR}/${APP_NAME}.app"
else
    codesign --force --sign - --timestamp=none "${STAGE_DIR}/${APP_NAME}.app"
fi
codesign --verify --deep --strict "${STAGE_DIR}/${APP_NAME}.app"

cat >"${STAGE_DIR}/README.txt" <<EOF
${APP_NAME} ${VERSION} for macOS (Apple Silicon)

Game data is not included. You need your own copy of Crash Team Racing for
the PlayStation, North American release (SCUS-94426), as a disc image.

Install:
- Drag ${APP_NAME} to the Applications folder.
- Open it. The first time, a setup window asks for your disc image: a .cue
  file (with its .bin next to it), a .bin file, or a .chd file.
- ${APP_NAME} copies the disc into its data folder, so you only do this once.

The disc image must be a raw single-track MODE2/2352 dump (what .cue/.bin
and .chd dumps of this disc are). A 2048-byte .iso is missing the audio and
video data and is not accepted.

Change the disc image: hold the Option key while opening ${APP_NAME}.

Data folder (disc image, settings, memory cards, logs):
  ~/Library/Application Support/${APP_NAME}

If macOS says the app "cannot be opened" or "is damaged" (builds that are not
notarized): right-click ${APP_NAME} in Applications, choose Open, then click
Open again. Or run once in Terminal:
  xattr -dr com.apple.quarantine /Applications/${APP_NAME}.app

Requirements: macOS 12 or later on an Apple Silicon Mac.

If the game stops with an error, the message offers a Show Log button. Please
include ctr_native-console.log and "Crash Team Racing.log" from the data folder
when reporting a problem.
EOF

# NOTE: Newer macOS deprecates `hdiutil create` for `diskutil image`; older
# macOS has no `diskutil image`.
if diskutil image 2>&1 | grep -q "Manipulate disk images"; then
    diskutil image create from --format UDZO --volumeName "${APP_NAME}" "${STAGE_DIR}" "${DMG_PATH}" >/dev/null
else
    hdiutil create -volname "${APP_NAME}" -srcfolder "${STAGE_DIR}" -fs HFS+ -format UDZO -ov "${DMG_PATH}" >/dev/null
fi

if [[ -n "${CODESIGN_IDENTITY:-}" ]]; then
    codesign --force --timestamp --sign "${CODESIGN_IDENTITY}" "${DMG_PATH}"

    if [[ -n "${NOTARY_PROFILE:-}" ]]; then
        xcrun notarytool submit "${DMG_PATH}" --keychain-profile "${NOTARY_PROFILE}" --wait
        xcrun stapler staple "${DMG_PATH}"
    fi
fi

rm -rf "${STAGE_DIR}"

(
    cd "${DIST_DIR}"
    shasum -a 256 "${PACKAGE_NAME}.dmg" >"${PACKAGE_NAME}.dmg.sha256"
)

echo "Wrote ${DMG_PATH}"
echo "Wrote ${DMG_PATH}.sha256"
