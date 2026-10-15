#!/usr/bin/env bash
# Fetch the pinned wasm toolchain used by the sandbox builds (macOS, Linux
# x86_64, Android). Installs into externals/.toolchains/ (gitignored).
#
# Build hosts: macOS on Apple Silicon and Linux on x86_64 (WABT ships no other
# Linux binaries).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${ROOT}/externals/.toolchains"

WASI_SDK_VERSION="25.0"
WASI_SDK_BASE="https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-25"
WABT_VERSION="1.0.37"
WABT_BASE="https://github.com/WebAssembly/wabt/releases/download/${WABT_VERSION}"

case "$(uname -s)-$(uname -m)" in
    Darwin-arm64)
        WASI_SDK_URL="${WASI_SDK_BASE}/wasi-sdk-${WASI_SDK_VERSION}-arm64-macos.tar.gz"
        WASI_SDK_SHA256="e1e529ea226b1db0b430327809deae9246b580fa3cae32d31c82dfe770233587"
        WABT_URL="${WABT_BASE}/wabt-${WABT_VERSION}-macos-14.tar.gz"
        WABT_SHA256="a996907cbdf1bdcd56d66d382dd1b031450bb7326bbfd0f43a04486b620bed1c"
        ;;
    Linux-x86_64)
        WASI_SDK_URL="${WASI_SDK_BASE}/wasi-sdk-${WASI_SDK_VERSION}-x86_64-linux.tar.gz"
        WASI_SDK_SHA256="52640dde13599bf127a95499e61d6d640256119456d1af8897ab6725bcf3d89c"
        WABT_URL="${WABT_BASE}/wabt-${WABT_VERSION}-ubuntu-20.04.tar.gz"
        WABT_SHA256="cfc675dc9b663d9adc8c75d982c1ba29f661448a2e026a67e71fe3f76a3e09f3"
        ;;
    *)
        echo "ERROR: no pinned wasm toolchain for $(uname -s) $(uname -m)" >&2
        echo "Build on macOS (Apple Silicon) or Linux (x86_64)." >&2
        exit 1
        ;;
esac

sha256() {
    if command -v sha256sum &>/dev/null; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

fetch() {
    local name="$1" url="$2" sha="$3" dir="$4"

    if [[ -d "${DEST}/${dir}" ]]; then
        echo "${name}: already installed (${DEST}/${dir})"
        return
    fi

    local tmp
    tmp="$(mktemp -d)"
    trap 'rm -rf "${tmp}"' RETURN

    echo "${name}: downloading ${url}"
    curl -fsSL -o "${tmp}/archive.tar.gz" "${url}"

    local actual
    actual="$(sha256 "${tmp}/archive.tar.gz")"
    if [[ "${actual}" != "${sha}" ]]; then
        echo "ERROR: ${name} sha256 mismatch: expected ${sha}, got ${actual}" >&2
        exit 1
    fi

    mkdir -p "${tmp}/out"
    tar -xzf "${tmp}/archive.tar.gz" -C "${tmp}/out"
    mkdir -p "${DEST}"
    mv "${tmp}/out/"* "${DEST}/${dir}"
    # NOTE: Downloaded binaries carry the quarantine attribute on macOS; strip it so they can run.
    if [[ "$(uname -s)" == Darwin ]]; then
        xattr -dr com.apple.quarantine "${DEST}/${dir}" 2>/dev/null || true
    fi
    echo "${name}: installed to ${DEST}/${dir}"
}

fetch "wasi-sdk" "${WASI_SDK_URL}" "${WASI_SDK_SHA256}" "wasi-sdk"
fetch "wabt" "${WABT_URL}" "${WABT_SHA256}" "wabt"
