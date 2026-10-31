// Which host the wasm sandbox build runs on.
//
// HOST_MACOS: the desktop macOS app (AppKit setup, desktop OpenGL).
// HOST_IOS:   iPhone and iPad (UIKit through SDL, OpenGL ES).
// HOST_GLES:  OpenGL ES 3.0 only (Android, iOS); see host_gles.h.
// HOST_MOBILE: one full-screen window and no desktop windowing (Android, iOS).

#ifndef CTR_WASM_HOST_PLATFORM_H
#define CTR_WASM_HOST_PLATFORM_H

#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#define HOST_IOS 1
#else
#define HOST_MACOS 1
#endif
#endif

#if defined(__ANDROID__) || defined(HOST_IOS)
#define HOST_GLES 1
#define HOST_MOBILE 1
#endif

#endif
