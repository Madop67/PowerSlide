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
// Keyboard (F1): opens the menu for keyboard and mouse, or closes it.
void NativeUI_ToggleMenu(void);
// Controller menu button (Home / L3+R3): opens the menu driven by that controller, closes it
// when that controller already drives it, or hands it over from another device.
void NativeUI_ToggleMenuFromController(unsigned int instanceId);
// The menu leaves the game running. These say which device it holds back from the game:
// the keyboard when it was opened from the keyboard, else the controller that opened it (0 = none).
int NativeUI_MenuOwnsKeyboard(void);
unsigned int NativeUI_GetMenuController(void);
// True while the input tab waits for a key or button to bind; menu hotkeys pause meanwhile.
int NativeUI_IsCapturingBinding(void);

// Platform callback: shows the cursor while the menu is open, hides it in fullscreen otherwise.
void Platform_UpdateCursorVisibility(void);
// Frames per second shown (see platform.h).
int Platform_GetTargetFrameRate(void);

#ifdef __cplusplus
}
#endif

#endif
