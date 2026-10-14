#ifndef NATIVE_TWEAKS_H
#define NATIVE_TWEAKS_H

// NOTE: Shared with the C++ UI layer; keep to plain C types.

#ifdef __cplusplus
extern "C" {
#endif

// Where a retail cheat applies.
enum NativeCheatScope
{
	// Everywhere, as retail leaves the flag on in every mode.
	NATIVE_CHEAT_SCOPE_ANY = 0,
	// Not in adventure or time trial: retail clears these flags when either mode starts.
	NATIVE_CHEAT_SCOPE_NOT_ADVENTURE_OR_TIME_TRIAL,
};

// One of retail's cheat codes as a Tweaks toggle. While its setting is on, the module
// keeps the cheat's gameMode2 flag set, as if the code had been entered.
struct NativeCheat
{
	// Cheats tab section the toggle is listed under.
	const char *group;
	const char *label;
	const char *tooltip;
	int *setting;
	// GameTracker gameMode2 flag (CHEAT_*).
	unsigned int flag;
	enum NativeCheatScope scope;
};

int NativeTweaks_CheatCount(void);
const struct NativeCheat *NativeTweaks_CheatAt(int index);

#ifdef __cplusplus
}
#endif

#endif
