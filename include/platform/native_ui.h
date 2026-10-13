#ifndef NATIVE_UI_H
#define NATIVE_UI_H

// NOTE: In-game settings menu (Dear ImGui). Implemented in C++
// (platform/native_ui.cpp); this C interface is all the platform layer sees.

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

int NativeUI_Init(SDL_Window *window, SDL_GLContext context);
void NativeUI_Shutdown(void);
// Feed every host event; the platform layer decides what still reaches the game.
void NativeUI_ProcessEvent(const SDL_Event *event);
// Draws the menu and overlays into the bound default framebuffer, before the swap.
void NativeUI_Render(void);
int NativeUI_IsMenuOpen(void);
void NativeUI_ToggleMenu(void);
// True while the input tab waits for a key or button to bind; menu hotkeys pause meanwhile.
int NativeUI_IsCapturingBinding(void);

// Platform callback: shows the cursor while the menu is open, hides it in fullscreen otherwise.
void Platform_UpdateCursorVisibility(void);

#ifdef __cplusplus
}
#endif

#endif
