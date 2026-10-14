#include <common.h>
#include <macros.h>

#include <platform/native_hooks.h>
#include <platform/native_settings.h>
#include <platform/native_tweaks.h>

// Tweaks tab gameplay options, all off by default:
// - game.unlock_all_characters: character select treats every character as unlocked.
//   The save's own unlocks are untouched, so turning it off locks them again.
// - The cheats in s_cheats switch on retail's own cheat flags (as if their codes were
//   entered); each takes effect where retail reads its flag, mostly when a race starts.
//   Retail clears the item cheats when adventure or time trial starts, so they stay off
//   there too.

#define NATIVE_ITEM_CHEAT_NOTE "Only one unlimited weapon is held: masks win over turbo, and turbo over bombs. "
#define NATIVE_ITEM_CHEAT_SCOPE "Takes effect from the next race; not used in adventure or time trial."

global_variable const struct NativeCheat s_cheats[] = {
	{"Items", "Unlimited wumpa fruit",
	 "Start every race with full wumpa fruit that is never lost, so weapons are always juiced up. " NATIVE_ITEM_CHEAT_SCOPE,
	 &g_settings.game.unlimitedWumpa, CHEAT_WUMPA, NATIVE_CHEAT_SCOPE_NOT_ADVENTURE_OR_TIME_TRIAL},
	{"Items", "Unlimited turbo", "Start every race holding turbos that never run out. " NATIVE_ITEM_CHEAT_NOTE NATIVE_ITEM_CHEAT_SCOPE,
	 &g_settings.game.unlimitedTurbo, CHEAT_TURBO, NATIVE_CHEAT_SCOPE_NOT_ADVENTURE_OR_TIME_TRIAL},
	{"Items", "Unlimited masks", "Start every race holding masks that never run out. " NATIVE_ITEM_CHEAT_NOTE NATIVE_ITEM_CHEAT_SCOPE,
	 &g_settings.game.unlimitedMasks, CHEAT_MASK, NATIVE_CHEAT_SCOPE_NOT_ADVENTURE_OR_TIME_TRIAL},
	{"Items", "Unlimited bombs", "Start every race holding bombs that never run out. " NATIVE_ITEM_CHEAT_NOTE NATIVE_ITEM_CHEAT_SCOPE,
	 &g_settings.game.unlimitedBombs, CHEAT_BOMBS, NATIVE_CHEAT_SCOPE_NOT_ADVENTURE_OR_TIME_TRIAL},
	{"Items", "Super engine", "Every kart, opponents included, has a super engine that never wears off. " NATIVE_ITEM_CHEAT_SCOPE,
	 &g_settings.game.superEngine, CHEAT_ENGINE, NATIVE_CHEAT_SCOPE_NOT_ADVENTURE_OR_TIME_TRIAL},
	{"Items", "Permanent invisibility", "Players stay invisible for the whole race; opponents do not. Takes effect from the next race.",
	 &g_settings.game.permanentInvisibility, CHEAT_INVISIBLE, NATIVE_CHEAT_SCOPE_ANY},
	{"Tracks", "Icy tracks", "Every track drives as if covered in ice.", &g_settings.game.icyTracks, CHEAT_ICY, NATIVE_CHEAT_SCOPE_ANY},
	{"Tracks", "Super turbo pads", "Turbo pads give the bigger boost of a super turbo pad.", &g_settings.game.superTurboPads, CHEAT_TURBOPAD,
	 NATIVE_CHEAT_SCOPE_ANY},
	{"Tracks", "One-lap race",
	 "Arcade and VS races are one lap. Takes effect when you next choose Arcade or VS; after turning it off, choose the lap count "
	 "again in track select.",
	 &g_settings.game.oneLapRace, CHEAT_ONELAP, NATIVE_CHEAT_SCOPE_ANY},
	{"Opponents", "Super hard", "Arcade opponents race at the hardest difficulty.", &g_settings.game.superHard, CHEAT_SUPERHARD,
	 NATIVE_CHEAT_SCOPE_ANY},
	{"Opponents", "Harder adventure", "Opponents in adventure races, cups and boss races are tougher.", &g_settings.game.adventureDifficulty,
	 CHEAT_ADV, NATIVE_CHEAT_SCOPE_ANY},
	{"HUD", "Turbo counter", "Shows how many boosts you have chained, in 1-player races.", &g_settings.game.turboCounter, CHEAT_TURBOCOUNT,
	 NATIVE_CHEAT_SCOPE_ANY},
};

#undef NATIVE_ITEM_CHEAT_NOTE
#undef NATIVE_ITEM_CHEAT_SCOPE

// Cheat flags this module turned on; the player's own cheat codes are left alone.
global_variable u32 s_tweaksOwnedCheats;

int NativeTweaks_CheatCount(void)
{
	return (int)(sizeof(s_cheats) / sizeof(s_cheats[0]));
}

const struct NativeCheat *NativeTweaks_CheatAt(int index)
{
	return ((index >= 0) && (index < NativeTweaks_CheatCount())) ? &s_cheats[index] : NULL;
}

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

	const int itemCheatsAllowed = (gGT->gameMode1 & (ADVENTURE_MODE | TIME_TRIAL)) == 0;
	for (int i = 0; i < NativeTweaks_CheatCount(); i++)
	{
		const struct NativeCheat *cheat = &s_cheats[i];
		const int allowed = (cheat->scope == NATIVE_CHEAT_SCOPE_ANY) || itemCheatsAllowed;
		NativeTweaks_SetCheat(gGT, cheat->flag, allowed && *cheat->setting);
	}
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
