#ifndef NATIVE_INPUT_CONFIG_H
#define NATIVE_INPUT_CONFIG_H

// Input-layer queries for the settings menu (C++); plain C types only.

#ifdef __cplusplus
extern "C" {
#endif

// Returns 1 and fills the controller's name and SDL_GamepadType when a
// controller drives player slot `player` (0-3).
int NativeInput_GetPlayerController(int player, char *name, int nameSize, int *gamepadType);
// Swaps which controllers drive two player slots; each player keeps its bindings and the
// remembered controllers move with the swap.
void NativeInput_SwapPlayers(int playerA, int playerB);
// SDL joystick instance id of the controller driving player slot `player`, or 0.
int NativeInput_GetPlayerControllerId(int player);
// Returns the player slot an SDL joystick instance drives, or -1.
int NativeInput_IsGamepadEventForPlayer(unsigned int instanceId);

#ifdef __cplusplus
}
#endif

#endif
