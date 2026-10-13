// Minimal POSIX-backed WASI preview1 host for the macOS wasm sandbox build.

#ifndef CTR_WASM_HOST_WASI_H
#define CTR_WASM_HOST_WASI_H

// Captures argv/environ for the guest and preopens "/" as WASI fd 3.
void HostWasi_Init(int argc, char **argv);

#endif
