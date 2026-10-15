#!/usr/bin/env bash
# Package the Linux x86_64 build (./build-linux-x64.sh) for release.
#
#   ./package-linux.sh [version]
#
# Writes dist/PowerSlide-<version>-linux-x86_64.tar.gz and its .sha256. The
# binary must stand alone: SDL3 and libchdr are static and SDL loads X11,
# Wayland and the audio libraries itself, so only glibc and libGL may remain.

set -euo pipefail

cd "$(dirname "$0")"

VERSION="${1:-$(sed -n 's/^set(CTR_NATIVE_VERSION "\(.*\)")$/\1/p' CMakeLists.txt)}"
BUILD_DIR="${BUILD_DIR:-build-linux-x64}"
DIST_DIR="${DIST_DIR:-dist}"
APP_NAME="PowerSlide"
BINARY_NAME="powerslide"
PACKAGE_NAME="${APP_NAME}-${VERSION}-linux-x86_64"
PACKAGE_DIR="${DIST_DIR}/${PACKAGE_NAME}"
BINARY_PATH="${BUILD_DIR}/ctr_native"

if [[ ! -x "${BINARY_PATH}" ]]; then
    echo "ERROR: missing executable: ${BINARY_PATH}" >&2
    echo "Run ./build-linux-x64.sh first." >&2
    exit 1
fi

python3 tools/release/check_standalone.py "${BINARY_PATH}"

rm -rf "${PACKAGE_DIR}"
mkdir -p "${PACKAGE_DIR}"

# The build target keeps the upstream name; players run the product name.
cp "${BINARY_PATH}" "${PACKAGE_DIR}/${BINARY_NAME}"
chmod +x "${PACKAGE_DIR}/${BINARY_NAME}"
cp LICENSE THIRD_PARTY_NOTICES.md "${PACKAGE_DIR}/"

cat >"${PACKAGE_DIR}/README.txt" <<README
${APP_NAME} ${VERSION} for Linux (x86_64)

Game data is not included. You need your own copy of Crash Team Racing for
the PlayStation, North American release (SCUS-94426), as a disc image.

Install:
- Extract this folder anywhere and run ./${BINARY_NAME}
- The first time, a setup dialog asks for your disc image: a .cue file (with
  its .bin next to it), a .bin file, or a .chd file. If your desktop has no
  file dialog (no XDG portal or zenity), drop the file onto the setup window.
- ${APP_NAME} copies the disc into its data folder, so you only do this once.

The disc image must be a raw single-track MODE2/2352 dump (what .cue/.bin
and .chd dumps of this disc are). A 2048-byte .iso is missing the audio and
video data and is not accepted.

Change the disc image: run ./${BINARY_NAME} --setup, or use
"Change disc image..." in the menu's About tab (F1).

Data folder (disc image, settings, memory cards, logs):
  \${XDG_DATA_HOME:-~/.local/share}/${APP_NAME}

Portable use: put the disc image at assets/ctr-u.bin next to ${BINARY_NAME}.
${APP_NAME} then keeps everything in its own folder and skips setup.

Requirements: 64-bit x86 Linux with glibc 2.35 or later (Ubuntu 22.04,
Debian 12, Fedora 36 or newer), X11 or Wayland, and OpenGL 3.3. No 32-bit
libraries are needed.

If the game stops with an error, the message offers a Show Log button. Please
include ctr_native-console.log and "Crash Team Racing.log" from the data folder
when reporting a problem.
README

(
    cd "${DIST_DIR}"
    tar -czf "${PACKAGE_NAME}.tar.gz" "${PACKAGE_NAME}"
    sha256sum "${PACKAGE_NAME}.tar.gz" >"${PACKAGE_NAME}.tar.gz.sha256"
)
rm -rf "${PACKAGE_DIR}"

echo "Wrote ${DIST_DIR}/${PACKAGE_NAME}.tar.gz"
echo "Wrote ${DIST_DIR}/${PACKAGE_NAME}.tar.gz.sha256"
