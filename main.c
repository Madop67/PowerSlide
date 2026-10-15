#define _CRT_SECURE_NO_WARNINGS
#define SDL_MAIN_HANDLED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include "platform/native_win32.h"
#else
#include <unistd.h>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#define _EnterCriticalSection(x)
#define EnterCriticalSection(x)
#define ExitCriticalSection()

#include "platform/native_assets.h"
#include "platform/native_hooks.h"
#include "platform/native_log.h"
#include "platform/native_memory.h"
#include "platform/native_perf.h"
#include "platform/native_product.h"
#include "platform/native_replay_scheduler.h"
#include "platform/native_savestate.h"
#include "platform/native_settings.h"

#include <platform.h>

#if defined(CTR_NATIVE_SETUP)
#include "setup/native_setup.h"
#endif

#include "game/game_unity.h"

#include "game/zGlobal_RDATA.c"
#include "game/zGlobal_DATA.c"
#include "game/zGlobal_SDATA.c"

#undef RECT

#include "platform/native_disc_image.c"
#include "platform/native_assets.c"
#include "platform/native_branding.c"
#include "platform/native_audio.c"
#include "platform/native_memory.c"
#include "platform/native_checkpoint.c"
#include "platform/native_checkpoint_file.c"
#include "platform/native_cd.c"
#include "platform/native_gpu_links.c"
#include "platform/native_gpu.c"
#include "platform/native_gte_core.c"
#include "platform/native_hooks.c"
#include "platform/native_fast_boot.c"
#include "platform/native_glad.c"
#include "platform/native_input.c"
#include "platform/native_inline_c.c"
#include "platform/native_interp.c"
#include "platform/native_libapi.c"
#include "platform/native_libetc.c"
#include "platform/native_libgte.c"
#include "platform/native_libgpu.c"
#include "platform/native_libpad.c"
#include "platform/native_libspu.c"
#include "platform/native_log.c"
#include "platform/native_memcard.c"
#include "platform/native_memcard_adapter.c"
#include "platform/native_perf.c"
#include "platform/native_platform.c"
#include "platform/native_replay_scheduler.c"
#include "platform/native_renderer.c"
#include "platform/native_savestate.c"
#include "platform/native_settings.c"
#include "platform/native_state.c"
#include "platform/native_str.c"
#include "platform/native_tweaks.c"

#if defined(CTR_NATIVE_WASM)
#include "platform/wasm/guest_wasm.c"
#endif

#ifndef CC
#if defined(__GNUC__)
#if _WIN32
#ifndef __clang__
#define CC "MINGW-GCC"
#else
#define CC "MINGW-CLANG"
#endif
#else
#ifndef __clang__
#define CC "GCC"
#else
#define CC "CLANG"
#endif
#endif
#elif defined(_MSC_VER)
#define CC "MSVC"
#else
#define CC "Unknown"
#endif
#endif

#ifndef CTR_NATIVE_VERSION
#define CTR_NATIVE_VERSION "0.0.0-dev"
#endif

#ifndef CTR_NATIVE_BUILD_ID
#define CTR_NATIVE_BUILD_ID "unknown"
#endif

static int NativeConsole_ShouldPauseOnError(void)
{
#if defined(_WIN32)
	DWORD consoleProcesses[2];
	DWORD consoleProcessCount;

	if (GetConsoleWindow() == NULL)
		return 0;

	consoleProcessCount = GetConsoleProcessList(consoleProcesses, (DWORD)(sizeof(consoleProcesses) / sizeof(consoleProcesses[0])));
	return (consoleProcessCount == 1) && (consoleProcesses[0] == GetCurrentProcessId());
#else
	return 0;
#endif
}

#if defined(_WIN32) && !defined(CTR_NATIVE_WASM)
// The exe is a GUI program, so a double-click opens no console window. Started
// from a terminal, it writes to that terminal; pipes (ctest, replay_suite.py)
// are inherited and used as they are.
static void NativeConsole_Attach(void)
{
	HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);

	if ((output != NULL) && (output != INVALID_HANDLE_VALUE))
	{
		return;
	}

	if (AttachConsole(ATTACH_PARENT_PROCESS))
	{
		(void)freopen("CONOUT$", "w", stdout);
		(void)freopen("CONOUT$", "w", stderr);
		(void)freopen("CONIN$", "r", stdin);
	}
}
#endif

#if defined(CTR_NATIVE_SETUP)
// Set when a packaged build runs from its per-user data folder; errors then
// also show a message box, since there is no console to read.
static char s_nativeSetupLogPath[4096];
static int s_nativeSetupInstalled;
#endif

static s32 NativeConsole_Return(const u32 result)
{
#if defined(CTR_NATIVE_SETUP)
	if ((result != 0) && s_nativeSetupInstalled)
	{
		char message[256];

		fflush(stdout);
		fflush(stderr);
		snprintf(message, sizeof(message), "The game quit with an error (code %u). The log file may say why.", (unsigned int)result);
		NativeSetup_ShowFatal(message, (s_nativeSetupLogPath[0] != '\0') ? s_nativeSetupLogPath : NULL);
		return (s32)result;
	}
#endif

	if ((result != 0) && NativeConsole_ShouldPauseOnError())
	{
		fflush(stdout);
		fflush(stderr);
		fprintf(stderr, "\n[CTR Native] Press Enter to close this window...");
		fflush(stderr);

		while (getchar() != '\n' && !feof(stdin))
		{
		}
	}

	return (s32)result;
}

// TODO(aalhendi): just make an argparser?
static int NativeArg_IsVersion(const char *arg)
{
	return (arg != NULL) && ((strcmp(arg, "--version") == 0) || (strcmp(arg, "-v") == 0));
}

static int NativeArg_IsPresent(int argc, char *argv[], const char *name)
{
	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		if (strcmp(argv[argIndex], name) == 0)
		{
			return 1;
		}
	}
	return 0;
}

static const char *NativeArg_GetValue(int argc, char *argv[], const char *name)
{
	for (int argIndex = 1; argIndex + 1 < argc; argIndex++)
	{
		if (strcmp(argv[argIndex], name) == 0)
		{
			return argv[argIndex + 1];
		}
	}
	return NULL;
}

int main(int argc, char *argv[])
{
	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		if (NativeArg_IsVersion(argv[argIndex]))
		{
			printf("%s %s (%s)\n", NATIVE_PRODUCT_NAME, CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
			return 0;
		}
	}

#if defined(_WIN32) && !defined(CTR_NATIVE_WASM)
	NativeConsole_Attach();
#endif

	// --base-dir replaces the executable's folder as the home of assets/,
	// settings, memory cards and the log (the macOS app bundle passes its
	// Application Support folder).
	const char *basePath = NativeArg_GetValue(argc, argv, "--base-dir");

#if defined(CTR_NATIVE_SETUP)
	// A packaged build with no assets/ next to it (or above it, as in a
	// checkout) runs from its per-user data folder, after first-run setup.
	static char setupBaseDir[4096];
	if ((basePath == NULL) && !NativeSetup_FindPortableBase(setupBaseDir, sizeof(setupBaseDir)))
	{
		if (!NativeSetup_Run(setupBaseDir, sizeof(setupBaseDir), NativeArg_IsPresent(argc, argv, "--setup")))
		{
			return 0;
		}

		NativeSetup_RedirectConsole(setupBaseDir, s_nativeSetupLogPath, sizeof(s_nativeSetupLogPath));
		NativeSetup_InstallCrashHandler(s_nativeSetupLogPath);
		s_nativeSetupInstalled = 1;
		basePath = setupBaseDir;
	}
#endif

	printf("[CTR Native] Starting...\n");
	fflush(stdout);

	const char *sdlBasePath = SDL_GetBasePath();
	printf("[CTR Native] SDL base path: %s\n", sdlBasePath ? sdlBasePath : "(null)");
	fflush(stdout);

	if (basePath == NULL)
	{
		basePath = sdlBasePath;
	}

	if (!NativeAssets_Init(basePath))
	{
		fprintf(stderr, "[CTR Native] Failed to initialize asset paths.\n");
		return NativeConsole_Return(1);
	}

	printf("[CTR Native] Version: %s (%s)\n", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
	printf("[CTR Native] Built with: " CC "\n");
	printf("[CTR Native] Base: %s\n", NativeAssets_GetBaseDir());
	printf("[CTR Native] Assets: %s\n", NativeAssets_GetAssetDir());
	fflush(stdout);

	if (chdir(NativeAssets_GetBaseDir()) != 0)
	{
		fprintf(stderr, "[CTR Native] Failed to enter base directory: %s\n", NativeAssets_GetBaseDir());
		return NativeConsole_Return(1);
	}

	if (!NativeAssets_Validate())
	{
		return NativeConsole_Return(1);
	}

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_PrepareReportFromArgs(argc, argv) != 0)
	{
		return NativeConsole_Return(1);
	}
#endif

	if (NativeArg_IsPresent(argc, argv, "--vanilla"))
	{
		NativeSettings_LoadVanilla();
	}
	else
	{
		NativeSettings_Load(NativeAssets_GetBaseDir());
	}
	if (!NativeSettings_ApplyOverrides(argc, argv))
	{
		Platform_LogFlush();
		return NativeConsole_Return(1);
	}

#ifdef USE_16BY9
	printf("[CTR Native] Widescreen\n");
	Platform_Init("Crash Team Racing", 1280, 720);
#else
	printf("[CTR Native] 4:3\n");
	Platform_Init("Crash Team Racing", 800, 600);
#endif

#if defined(CTR_INTERNAL)
	if (NativePerf_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
#endif

	Platform_InitScratchpad();
	Platform_RepairResidentPointers(0);

#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_ConfigureFromArgs(argc, argv) != 0)
	{
		Platform_LogFlush();
		Platform_Shutdown();
		return NativeConsole_Return(1);
	}
	for (int argIndex = 1; argIndex < argc; argIndex++)
	{
		if ((strcmp(argv[argIndex], "--input-script") == 0) && ((argIndex + 1 >= argc) || !Platform_InputLoadScript(argv[argIndex + 1])))
		{
			Platform_LogFlush();
			Platform_Shutdown();
			return NativeConsole_Return(1);
		}
		if ((strcmp(argv[argIndex], "--screenshot-every") == 0) && (argIndex + 1 < argc))
		{
			Platform_SetScreenshotInterval((unsigned int)strtoul(argv[argIndex + 1], NULL, 10));
		}
	}
#else
	(void)argc;
	(void)argv;
#endif

	// Packaged launches (the hosts pass --packaged) can ask for a new disc image.
	if (NativeArg_IsPresent(argc, argv, "--packaged")
#if defined(CTR_NATIVE_SETUP)
	    || s_nativeSetupInstalled
#endif
	)
	{
		NativeUI_EnableDiscChange();
	}

	NativeHooks_RegisterModules();

	int result = CTR_Main();
#if defined(CTR_INTERNAL)
	if (NativeReplayScheduler_GetExitCode() != 0)
	{
		result = NativeReplayScheduler_GetExitCode();
	}
#endif

	Platform_Shutdown();
	return NativeConsole_Return(result);
}
