// First-run setup for the packaged Windows, Linux and Android builds.
//
// The counterpart of the macOS app bundle's AppKit setup window
// (platform/wasm_host/host_setup_macos.m), built on SDL3 alone: picks the
// per-user data folder and, when it has no usable disc image yet, asks for
// one and imports it (platform/setup/disc_import.c). Runs before the game
// initialises SDL itself and shuts SDL down again when it is done.

#ifndef CTR_SETUP_NATIVE_SETUP_H
#define CTR_SETUP_NATIVE_SETUP_H

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

// Development and portable layout: returns 1 and writes the folder when the
// executable's folder, its parent or its grandparent has assets/ (as
// NativeAssets_Init looks for it). Such a launch skips setup entirely.
int NativeSetup_FindPortableBase(char *baseDir, size_t baseDirSize);

// Writes the data folder to baseDir, running setup when it has no usable disc
// image, when force is set (--setup), when Shift is held at launch (Windows),
// or when the menu asked for it (see NATIVE_SETUP_REQUEST_FILE). Returns 1 to start the game, 0 if the user quit
// or the folder could not be created.
int NativeSetup_Run(char *baseDir, size_t baseDirSize, int force);

// Sends stdout and stderr to <baseDir>/ctr_native-console.log and writes that
// path to logPath (empty if the log could not be opened).
void NativeSetup_RedirectConsole(const char *baseDir, char *logPath, size_t logPathSize);

// Shows a modal error box; logPath, if given, gets a "Show Log" button.
void NativeSetup_ShowFatal(const char *message, const char *logPath);

// Windows: shows "The game crashed" (with Show Log) on an unhandled exception,
// like the macOS bundle does when the guest traps. Does nothing elsewhere.
void NativeSetup_InstallCrashHandler(const char *logPath);

#if defined(__cplusplus)
}
#endif

#endif
