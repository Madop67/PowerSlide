// Disc image import for first-run setup (macOS app bundle, Windows, Linux
// and Android).
//
// Turns the user's own disc image (.bin, .cue or .chd) into the raw
// MODE2/2352 assets/ctr-u.bin the game reads. Host-only: never compiled into
// the wasm guest. Files go through SDL's IO and filesystem API, so paths are
// UTF-8 everywhere and Android content:// URIs open like any other path.

#ifndef CTR_SETUP_DISC_IMPORT_H
#define CTR_SETUP_DISC_IMPORT_H

#include <stddef.h>
#include <stdint.h>

#define DISC_IMPORT_BIN_NAME "ctr-u.bin"

typedef void (*DiscImportProgressFn)(void *user, uint64_t done, uint64_t total);

// Returns 1 if path is a raw single-track BIN of the NTSC-U disc (SCUS_944.26).
// On failure, writes a user-facing reason to err.
int DiscImport_Check(const char *path, char *err, size_t errSize);

// Converts or copies src into assetsDir/ctr-u.bin (via a .part file that is
// checked, then renamed). cancel may be NULL; set *cancel to stop early.
// Returns 1 on success; on failure, writes a user-facing reason to err.
int DiscImport_Run(const char *src, const char *assetsDir, DiscImportProgressFn progress, void *user, volatile int *cancel, char *err, size_t errSize);

#endif
