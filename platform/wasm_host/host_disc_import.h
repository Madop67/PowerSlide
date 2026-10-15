// Disc image import for the macOS app bundle's first-run setup.
//
// Turns the user's own disc image (.bin, .cue or .chd) into the raw
// MODE2/2352 assets/ctr-u.bin the game reads. Host-only: never compiled into
// the wasm guest.

#ifndef CTR_WASM_HOST_DISC_IMPORT_H
#define CTR_WASM_HOST_DISC_IMPORT_H

#include <stddef.h>
#include <stdint.h>

#define HOST_DISC_IMPORT_BIN_NAME "ctr-u.bin"

typedef void (*HostDiscImportProgressFn)(void *user, uint64_t done, uint64_t total);

// Returns 1 if path is a raw single-track BIN of the NTSC-U disc (SCUS_944.26).
// On failure, writes a user-facing reason to err.
int HostDiscImport_Check(const char *path, char *err, size_t errSize);

// Converts or copies src into assetsDir/ctr-u.bin (via a .part file that is
// checked, then renamed). cancel may be NULL; set *cancel to stop early.
// Returns 1 on success; on failure, writes a user-facing reason to err.
int HostDiscImport_Run(const char *src, const char *assetsDir, HostDiscImportProgressFn progress, void *user, volatile int *cancel, char *err,
                       size_t errSize);

#endif
