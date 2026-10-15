#include <platform/native_hooks.h>

#include <macros.h>

#include "platform/native_log.h"

#define NATIVE_HOOK_MAX_HANDLERS 8

#define NATIVE_HOOK_DEFINE(name, params, args)                                              \
	global_variable NativeHook_##name##_Fn s_hook##name##Handlers[NATIVE_HOOK_MAX_HANDLERS]; \
	global_variable int s_hook##name##Count;                                                 \
                                                                                             \
	int NativeHook_Register##name(NativeHook_##name##_Fn handler)                            \
	{                                                                                        \
		if ((handler == NULL) || (s_hook##name##Count >= NATIVE_HOOK_MAX_HANDLERS))          \
		{                                                                                    \
			Platform_LogWarn("[CTR Native] Hooks: cannot register another " #name " handler\n"); \
			return 0;                                                                        \
		}                                                                                    \
		s_hook##name##Handlers[s_hook##name##Count++] = handler;                             \
		return 1;                                                                            \
	}                                                                                        \
                                                                                             \
	void NativeHook_Call##name params                                                        \
	{                                                                                        \
		for (int i = 0; i < s_hook##name##Count; i++)                                        \
		{                                                                                    \
			s_hook##name##Handlers[i] args;                                                  \
		}                                                                                    \
	}
NATIVE_HOOKS(NATIVE_HOOK_DEFINE)
#undef NATIVE_HOOK_DEFINE

void NativeInterp_RegisterHooks(void);
void NativeFastBoot_RegisterHooks(void);
void NativeTweaks_RegisterHooks(void);
void NativeSecondScreen_RegisterHooks(void);

void NativeHooks_RegisterModules(void)
{
	NativeInterp_RegisterHooks();
	NativeFastBoot_RegisterHooks();
	NativeTweaks_RegisterHooks();
	NativeSecondScreen_RegisterHooks();
}
