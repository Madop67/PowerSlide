#include <platform/native_hooks.h>
#include <platform/native_settings.h>

#include <macros.h>

#include "platform/native_log.h"
#include "platform/native_replay_scheduler.h"

// Fast boot (game.fast_boot): "title" skips the SCEA and copyright screens and the
// Naughty Dog crate intro; "menu" also skips the title animation. Off by default.

global_variable int s_fastBootSkipTitlePending;

internal void NativeFastBoot_OnBoot(struct NativeBootHook *boot)
{
	const int mode = g_settings.game.fastBoot;

	s_fastBootSkipTitlePending = 0;
	if (mode == NATIVE_FAST_BOOT_OFF)
	{
		return;
	}

#if defined(CTR_INTERNAL)
	// Replays record and play the retail boot, whatever the setting.
	if (NativeReplayScheduler_IsActive())
	{
		Platform_Log("[CTR Native] Fast boot is off while recording or playing a replay\n");
		return;
	}
#endif

	boot->skipIntro = 1;
	s_fastBootSkipTitlePending = (mode == NATIVE_FAST_BOOT_MENU);
}

// Only the first title after boot is skipped; after attract mode it plays as usual.
internal void NativeFastBoot_OnTitleIntro(int *skipIntro)
{
	if (s_fastBootSkipTitlePending)
	{
		*skipIntro = 1;
		s_fastBootSkipTitlePending = 0;
	}
}

void NativeFastBoot_RegisterHooks(void)
{
	NativeHook_RegisterBoot(NativeFastBoot_OnBoot);
	NativeHook_RegisterTitleIntro(NativeFastBoot_OnTitleIntro);
}
