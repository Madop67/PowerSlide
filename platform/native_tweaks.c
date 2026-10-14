#include <common.h>
#include <macros.h>

#include <platform/native_hooks.h>
#include <platform/native_settings.h>

// Tweaks tab gameplay options, all off by default:
// - game.unlock_all_characters: character select treats every character as unlocked.
//   The save's own unlocks are untouched, so turning it off locks them again.
// - game.unlimited_turbo / game.unlimited_wumpa: switch on retail's own turbo and wumpa
//   cheats (as if their codes were entered), which take effect when a race starts.
//   Time trial has no items, so they stay off there, as retail clears them.

// Cheat flags this module turned on; the player's own cheat codes are left alone.
global_variable u32 s_tweaksOwnedCheats;

internal void NativeTweaks_SetCheat(struct GameTracker *gGT, u32 flag, int enabled)
{
	if (enabled)
	{
		if ((gGT->gameMode2 & flag) == 0)
		{
			gGT->gameMode2 |= flag;
			s_tweaksOwnedCheats |= flag;
		}
	}
	else if ((s_tweaksOwnedCheats & flag) != 0)
	{
		gGT->gameMode2 &= ~flag;
		s_tweaksOwnedCheats &= ~flag;
	}
}

internal void NativeTweaks_OnBeforeDraw(struct GameTracker *gGT, struct GamepadSystem *gGamepads)
{
	(void)gGamepads;

	const int allowed = (gGT->gameMode1 & TIME_TRIAL) == 0;
	NativeTweaks_SetCheat(gGT, CHEAT_TURBO, allowed && g_settings.game.unlimitedTurbo);
	NativeTweaks_SetCheat(gGT, CHEAT_WUMPA, allowed && g_settings.game.unlimitedWumpa);
}

internal void NativeTweaks_OnCharacterUnlocked(int *unlocked)
{
	if (g_settings.game.unlockAllCharacters)
	{
		*unlocked = 1;
	}
}

void NativeTweaks_RegisterHooks(void)
{
	NativeHook_RegisterBeforeDraw(NativeTweaks_OnBeforeDraw);
	NativeHook_RegisterCharacterUnlocked(NativeTweaks_OnCharacterUnlocked);
}
