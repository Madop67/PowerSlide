#ifndef NATIVE_HOOKS_H
#define NATIVE_HOOKS_H

// Hook bus: named points in the game code that native modules subscribe to, so
// enhancements live in their own files instead of inside decompiled functions.
//
// Game code calls a hook with NATIVE_HOOK(Name, args...), always inside
// #if defined(CTR_NATIVE) so the matching PS1 build never sees it. A module
// subscribes with NativeHook_Register<Name>(handler), usually from its
// <Module>_RegisterHooks function, which NativeHooks_RegisterModules calls once
// at startup. Handlers run in registration order.

#ifdef __cplusplus
extern "C" {
#endif

struct GameTracker;
struct GamepadSystem;

// Boot, before the first level loads. Handlers set the fields to change the boot.
struct NativeBootHook
{
	// Skip the SCEA and copyright screens and the Naughty Dog crate intro; load the
	// main menu first.
	int skipIntro;
};

// X(Name, parameter list, argument list)
#define NATIVE_HOOKS(X)                                                                            \
	/* In StateZero, before the logo screen and the first level load. */                          \
	X(Boot, (struct NativeBootHook * boot), (boot))                                                \
	/* The title animation starts (after boot or attract mode); set *skipIntro to start at the */ \
	/* menu, as if a button had been pressed. */                                                  \
	X(TitleIntro, (int *skipIntro), (skipIntro))                                                   \
	/* After the game logic of a 30 Hz tick, before its frame is drawn. */                        \
	X(BeforeDraw, (struct GameTracker * gGT, struct GamepadSystem * gGamepads), (gGT, gGamepads)) \
	/* After the tick's frame is presented. */                                                    \
	X(AfterPresent, (struct GameTracker * gGT), (gGT))

#define NATIVE_HOOK_DECLARE(name, params, args)                  \
	typedef void(*NativeHook_##name##_Fn) params;                \
	int NativeHook_Register##name(NativeHook_##name##_Fn handler); \
	void NativeHook_Call##name params;
NATIVE_HOOKS(NATIVE_HOOK_DECLARE)
#undef NATIVE_HOOK_DECLARE

#define NATIVE_HOOK(name, ...) NativeHook_Call##name(__VA_ARGS__)

// Lets every native module register its handlers. Called once, before the game starts.
void NativeHooks_RegisterModules(void);

#ifdef __cplusplus
}
#endif

#endif
