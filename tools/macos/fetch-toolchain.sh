#!/usr/bin/env bash
# Fetch the pinned wasm toolchain used by the macOS sandbox build.
# Installs into externals/.toolchains/ (gitignored).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEST="${ROOT}/externals/.toolchains"

WASI_SDK_VERSION="25.0"
WASI_SDK_URL="https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-25/wasi-sdk-${WASI_SDK_VERSION}-arm64-macos.tar.gz"
WASI_SDK_SHA256="e1e529ea226b1db0b430327809deae9246b580fa3cae32d31c82dfe770233587"

WABT_VERSION="1.0.37"
WABT_URL="https://github.com/WebAssembly/wabt/releases/download/${WABT_VERSION}/wabt-${WABT_VERSION}-macos-14.tar.gz"
WABT_SHA256="a996907cbdf1bdcd56d66d382dd1b031450bb7326bbfd0f43a04486b620bed1c"

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
    actual="$(shasum -a 256 "${tmp}/archive.tar.gz" | awk '{print $1}')"
    if [[ "${actual}" != "${sha}" ]]; then
        echo "ERROR: ${name} sha256 mismatch: expected ${sha}, got ${actual}" >&2
        exit 1
    fi

    mkdir -p "${tmp}/out"
    tar -xzf "${tmp}/archive.tar.gz" -C "${tmp}/out"
    mkdir -p "${DEST}"
    mv "${tmp}/out/"* "${DEST}/${dir}"
    # NOTE: Downloaded binaries carry the quarantine attribute; strip it so they can run.
    xattr -dr com.apple.quarantine "${DEST}/${dir}" 2>/dev/null || true
    echo "${name}: installed to ${DEST}/${dir}"
}

fetch "wasi-sdk" "${WASI_SDK_URL}" "${WASI_SDK_SHA256}" "wasi-sdk"
fetch "wabt" "${WABT_URL}" "${WABT_SHA256}" "wabt"
