// First-run setup for the macOS app bundle (PowerSlide.app).
//
// Runs natively in the host, before the wasm guest starts: picks the data
// folder in ~/Library/Application Support and, when it has no usable disc
// image yet (or Option is held at launch), shows a window that imports one.

#ifndef CTR_WASM_HOST_SETUP_MACOS_H
#define CTR_WASM_HOST_SETUP_MACOS_H

#include <stddef.h>

// Returns 1 when running from an app bundle (Info.plist with a bundle id).
int HostSetup_IsBundled(void);

// Writes the data folder to baseDir. Returns 1 to start the game, 0 if the
// user quit or the folder could not be created.
int HostSetup_Run(char *baseDir, size_t baseDirSize);

// Shows a modal error alert; logPath, if given, gets a "Show Log" button.
void HostSetup_ShowFatal(const char *message, const char *logPath);

#endif
