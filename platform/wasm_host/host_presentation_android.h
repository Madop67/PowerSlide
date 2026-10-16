// Android second display for the Dual Screen tab. See host_presentation_android.c.
//
// SDL on Android has one display and one window. host_sdl.c reports one more
// display (the one PresentationBridge.java found) and hands the guest a stand-in
// window and GL context for it, so native_second_screen.c runs unchanged.

#ifndef CTR_WASM_HOST_PRESENTATION_ANDROID_H
#define CTR_WASM_HOST_PRESENTATION_ANDROID_H

#include <stdbool.h>

#include <SDL3/SDL.h>

// Ids no SDL display or window gets (SDL counts up from 1).
#define HOST_PRESENTATION_DISPLAY_ID ((SDL_DisplayID)0x50530001u)
#define HOST_PRESENTATION_WINDOW_ID  ((SDL_WindowID)0x50530001u)

// Display (any thread).
bool HostPresentation_HasDisplay(void);
// Copied into a static buffer; NULL without a display.
const char *HostPresentation_DisplayName(void);
bool HostPresentation_DisplayBounds(SDL_Rect *rect);

// Window: a stand-in pointer the guest gets as a handle.
bool HostPresentation_IsWindow(const void *ptr);
void *HostPresentation_CreateWindow(void);
void HostPresentation_DestroyWindow(void);
// The surface's size in pixels (0 x 0 until it exists).
void HostPresentation_GetSize(int *width, int *height);

// GL context, shared with SDL's (game thread only).
bool HostPresentation_IsContext(const void *ptr);
void *HostPresentation_CreateContext(void);
bool HostPresentation_DestroyContext(void);
bool HostPresentation_MakeCurrent(void);
bool HostPresentation_IsCurrent(void);
// The context as the guest's handle points at it, or NULL.
void *HostPresentation_Context(void);
// The guest made an SDL window current.
void HostPresentation_LostCurrent(void);
bool HostPresentation_Swap(void);
bool HostPresentation_SetSwapInterval(int interval);

#endif
