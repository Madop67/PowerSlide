// iOS file picker for first-run setup (native_setup_ios.m). SDL has no file
// dialog on iOS, so native_setup.c calls this instead.

#ifndef CTR_SETUP_NATIVE_SETUP_IOS_H
#define CTR_SETUP_NATIVE_SETUP_IOS_H

#include <stddef.h>

// Shows the system document picker and waits for it. Returns 1 with the
// path of a local copy of the chosen file, 0 if the user cancelled.
int NativeSetupIOS_PickFile(char *path, size_t pathSize);

// Deletes the local copy the last pick made, once it has been imported.
void NativeSetupIOS_ForgetPicked(void);

#endif
