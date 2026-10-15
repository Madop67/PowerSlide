// First-run setup for the packaged Windows, Linux and Android builds. See
// native_setup.h.
//
// The flow mirrors the macOS setup window with what SDL offers everywhere:
// a message box explains what is needed, the system file dialog picks the
// image (Android: the storage access framework picker), and a small window
// shows import progress, drawn with SDL_Renderer's built-in debug font.

#include "native_setup.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <io.h>
#include <wchar.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "disc_import.h"
#include "platform/native_product.h"

#define NATIVE_SETUP_TITLE       NATIVE_PRODUCT_NAME " Setup"
#define NATIVE_SETUP_PATH_MAX    4096
#define NATIVE_SETUP_CONSOLE_LOG "ctr_native-console.log"

// Platform wording for the setup text.
#if defined(SDL_PLATFORM_ANDROID)
#define NATIVE_SETUP_IMAGE_KINDS "a .bin or .chd file"
#define NATIVE_SETUP_CANCEL_KEY  "Back"
#define NATIVE_SETUP_COPIED_TO   "It is copied into the app's storage"
#elif defined(SDL_PLATFORM_WINDOWS)
#define NATIVE_SETUP_IMAGE_KINDS "a .cue/.bin pair or a .chd file"
#define NATIVE_SETUP_COPIED_TO   "It is copied to your AppData folder"
#define NATIVE_SETUP_CANCEL_KEY  "Esc"
#else
#define NATIVE_SETUP_IMAGE_KINDS "a .cue/.bin pair or a .chd file"
#define NATIVE_SETUP_COPIED_TO   "It is copied to ~/.local/share"
#define NATIVE_SETUP_CANCEL_KEY  "Esc"
#endif

enum
{
	NATIVE_SETUP_BUTTON_QUIT = 0,
	NATIVE_SETUP_BUTTON_CHOOSE = 1,
	NATIVE_SETUP_BUTTON_OK = 0,
	NATIVE_SETUP_BUTTON_SHOW_LOG = 1,
};

// ---- Paths ------------------------------------------------------------------

static void NativeSetup_TrimTrailingSeparator(char *path)
{
	size_t len = SDL_strlen(path);

	while ((len > 1) && ((path[len - 1] == '/') || (path[len - 1] == '\\')))
	{
		path[--len] = '\0';
	}
}

static int NativeSetup_IsFile(const char *path)
{
	SDL_PathInfo info;
	return SDL_GetPathInfo(path, &info) && (info.type == SDL_PATHTYPE_FILE);
}

static int NativeSetup_HasAssets(const char *dir)
{
	char path[NATIVE_SETUP_PATH_MAX];

	SDL_snprintf(path, sizeof(path), "%s/assets/" DISC_IMPORT_BIN_NAME, dir);
	if (NativeSetup_IsFile(path))
	{
		return 1;
	}

	SDL_snprintf(path, sizeof(path), "%s/assets/BIGFILE.BIG", dir);
	return NativeSetup_IsFile(path);
}

static int NativeSetup_Parent(char *dir)
{
	NativeSetup_TrimTrailingSeparator(dir);

	char *slash = SDL_strrchr(dir, '/');
	char *backslash = SDL_strrchr(dir, '\\');
	if ((backslash != NULL) && ((slash == NULL) || (backslash > slash)))
	{
		slash = backslash;
	}

	if ((slash == NULL) || (slash == dir))
	{
		return 0;
	}

	*slash = '\0';
	return 1;
}

int NativeSetup_FindPortableBase(char *baseDir, size_t baseDirSize)
{
#if defined(SDL_PLATFORM_ANDROID)
	(void)baseDir;
	(void)baseDirSize;
	return 0;
#else
	char dir[NATIVE_SETUP_PATH_MAX];
	const char *exeDir = SDL_GetBasePath();

	if ((exeDir == NULL) || (SDL_strlcpy(dir, exeDir, sizeof(dir)) >= sizeof(dir)))
	{
		return 0;
	}

	NativeSetup_TrimTrailingSeparator(dir);

	// NOTE: Mirrors NativeAssets_Init: the executable's folder, its parent and
	// its grandparent (build/ and build/Release/ in a checkout).
	for (int level = 0; level < 3; level++)
	{
		if (NativeSetup_HasAssets(dir))
		{
			return SDL_strlcpy(baseDir, dir, baseDirSize) < baseDirSize;
		}

		if (!NativeSetup_Parent(dir))
		{
			break;
		}
	}

	return 0;
#endif
}

static int NativeSetup_GetDataDir(char *baseDir, size_t baseDirSize)
{
	const char *path = NULL;
	char *prefPath = NULL;

#if defined(SDL_PLATFORM_ANDROID)
	// App-specific external storage: players can reach memory cards and logs
	// over USB, and the app needs no storage permission.
	if ((SDL_GetAndroidExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) != 0)
	{
		path = SDL_GetAndroidExternalStoragePath();
	}

	if (path == NULL)
	{
		path = SDL_GetAndroidInternalStoragePath();
	}
#else
	// %APPDATA%\PowerSlide on Windows, ~/.local/share/PowerSlide on Linux.
	prefPath = SDL_GetPrefPath(NULL, NATIVE_PRODUCT_NAME);
	path = prefPath;
#endif

	int ok = (path != NULL) && (SDL_strlcpy(baseDir, path, baseDirSize) < baseDirSize) && SDL_CreateDirectory(path);
	SDL_free(prefPath);

	if (ok)
	{
		NativeSetup_TrimTrailingSeparator(baseDir);
	}

	return ok;
}

// ---- Message boxes ----------------------------------------------------------

static int NativeSetup_Ask(SDL_MessageBoxFlags flags, const char *message, const char *chooseLabel, const char *quitLabel)
{
	const SDL_MessageBoxButtonData buttons[] = {
		{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, NATIVE_SETUP_BUTTON_QUIT, quitLabel},
		{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, NATIVE_SETUP_BUTTON_CHOOSE, chooseLabel},
	};
	const SDL_MessageBoxData data = {
		flags | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT, NULL, NATIVE_SETUP_TITLE, message, SDL_arraysize(buttons), buttons, NULL,
	};

	int button = NATIVE_SETUP_BUTTON_QUIT;
	if (!SDL_ShowMessageBox(&data, &button))
	{
		SDL_Log("[CTR Native] setup: message box failed: %s", SDL_GetError());
		return 0;
	}

	return button == NATIVE_SETUP_BUTTON_CHOOSE;
}

void NativeSetup_ShowFatal(const char *message, const char *logPath)
{
	char text[2048];

#if defined(SDL_PLATFORM_ANDROID)
	// NOTE: Android won't open file:// URLs from an app, so name the file.
	if (logPath != NULL)
	{
		SDL_snprintf(text, sizeof(text), "%s\n\nLog: %s", message, logPath);
		message = text;
		logPath = NULL;
	}
#else
	SDL_strlcpy(text, message, sizeof(text));
#endif

	const SDL_MessageBoxButtonData buttons[] = {
		{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, NATIVE_SETUP_BUTTON_OK, "OK"},
		{0, NATIVE_SETUP_BUTTON_SHOW_LOG, "Show Log"},
	};
	const SDL_MessageBoxData data = {
		SDL_MESSAGEBOX_ERROR | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT, NULL, NATIVE_PRODUCT_NAME " stopped", message, (logPath != NULL) ? 2 : 1, buttons, NULL,
	};

	int button = NATIVE_SETUP_BUTTON_OK;
	if (!SDL_ShowMessageBox(&data, &button) || (button != NATIVE_SETUP_BUTTON_SHOW_LOG) || (logPath == NULL))
	{
		return;
	}

	// file:///C:/Users/... on Windows, file:///home/... elsewhere.
	char url[NATIVE_SETUP_PATH_MAX + 16];
	SDL_snprintf(url, sizeof(url), "file://%s%s", (logPath[0] == '/') ? "" : "/", logPath);
	for (char *p = url; *p != '\0'; p++)
	{
		if (*p == '\\')
		{
			*p = '/';
		}
	}

	if (!SDL_OpenURL(url))
	{
		SDL_Log("[CTR Native] setup: could not open %s: %s", url, SDL_GetError());
	}
}

// ---- Console log ------------------------------------------------------------

void NativeSetup_RedirectConsole(const char *baseDir, char *logPath, size_t logPathSize)
{
	if (SDL_snprintf(logPath, logPathSize, "%s/" NATIVE_SETUP_CONSOLE_LOG, baseDir) >= (int)logPathSize)
	{
		logPath[0] = '\0';
		return;
	}

	fflush(stdout);
	fflush(stderr);

#if defined(_WIN32)
	// NOTE: The data folder may have non-ASCII characters (the user name). A
	// GUI process has no valid stderr descriptor to dup2 onto, so both streams
	// reopen the emptied log for appending, unbuffered to keep their order.
	wchar_t *widePath = (wchar_t *)SDL_iconv_string("UTF-16LE", "UTF-8", logPath, SDL_strlen(logPath) + 1);
	FILE *truncated = (widePath != NULL) ? _wfopen(widePath, L"w") : NULL;
	int ok = truncated != NULL;
	if (ok)
	{
		fclose(truncated);
		ok = (_wfreopen(widePath, L"a", stdout) != NULL) && (_wfreopen(widePath, L"a", stderr) != NULL);
	}
	SDL_free(widePath);

	if (ok)
	{
		setvbuf(stdout, NULL, _IONBF, 0);
		setvbuf(stderr, NULL, _IONBF, 0);
	}
	else
	{
		logPath[0] = '\0';
	}
#else
	int fd = open(logPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	int ok = fd >= 0;
	if (ok)
	{
		ok = (dup2(fd, STDOUT_FILENO) >= 0) && (dup2(fd, STDERR_FILENO) >= 0);
		close(fd);
	}

	if (ok)
	{
		setvbuf(stdout, NULL, _IOLBF, 4096);
		setvbuf(stderr, NULL, _IONBF, 0);
	}
	else
	{
		logPath[0] = '\0';
	}
#endif
}

// ---- Crash handler ----------------------------------------------------------

#if defined(_WIN32)
static char s_nativeSetupCrashLog[NATIVE_SETUP_PATH_MAX];

static LONG WINAPI NativeSetup_OnCrash(EXCEPTION_POINTERS *info)
{
	char message[256];

	// NOTE: Best effort; the process may be in a bad state.
	SDL_snprintf(message, sizeof(message), "The game crashed (exception 0x%08lX). The log file may say why.",
	             (unsigned long)info->ExceptionRecord->ExceptionCode);
	fflush(stdout);
	fprintf(stderr, "[CTR Native] %s\n", message);
	fflush(stderr);
	NativeSetup_ShowFatal(message, (s_nativeSetupCrashLog[0] != '\0') ? s_nativeSetupCrashLog : NULL);
	return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void NativeSetup_InstallCrashHandler(const char *logPath)
{
#if defined(_WIN32)
	SDL_strlcpy(s_nativeSetupCrashLog, (logPath != NULL) ? logPath : "", sizeof(s_nativeSetupCrashLog));
	SetUnhandledExceptionFilter(NativeSetup_OnCrash);
#else
	(void)logPath;
#endif
}

// ---- File dialog ------------------------------------------------------------

enum NativeSetupPick
{
	NATIVE_SETUP_PICK_PENDING,
	NATIVE_SETUP_PICK_CHOSEN,
	NATIVE_SETUP_PICK_CANCELLED,
	NATIVE_SETUP_PICK_UNAVAILABLE,
};

struct NativeSetupDialog
{
	SDL_AtomicInt state;
	char path[NATIVE_SETUP_PATH_MAX];
};

// NOTE: SDL may call this from another thread (the portal's D-Bus thread on
// Linux), so the result is published through an atomic.
static void SDLCALL NativeSetup_OnFileChosen(void *user, const char *const *files, int filter)
{
	struct NativeSetupDialog *dialog = user;
	(void)filter;

	if (files == NULL)
	{
		SDL_Log("[CTR Native] setup: file dialog unavailable: %s", SDL_GetError());
		SDL_SetAtomicInt(&dialog->state, NATIVE_SETUP_PICK_UNAVAILABLE);
	}
	else if ((files[0] == NULL) || (SDL_strlcpy(dialog->path, files[0], sizeof(dialog->path)) >= sizeof(dialog->path)))
	{
		SDL_SetAtomicInt(&dialog->state, NATIVE_SETUP_PICK_CANCELLED);
	}
	else
	{
		SDL_SetAtomicInt(&dialog->state, NATIVE_SETUP_PICK_CHOSEN);
	}
}

static enum NativeSetupPick NativeSetup_PickFile(char *path, size_t pathSize)
{
	static struct NativeSetupDialog dialog;

#if defined(SDL_PLATFORM_ANDROID)
	// NOTE: The Android picker filters by MIME type, and .bin/.chd have none.
	const SDL_DialogFileFilter *filters = NULL;
	int filterCount = 0;
#else
	static const SDL_DialogFileFilter filters[] = {
		{"Disc images (.cue, .bin, .chd)", "cue;bin;chd"},
	};
	int filterCount = SDL_arraysize(filters);
#endif

	SDL_SetAtomicInt(&dialog.state, NATIVE_SETUP_PICK_PENDING);
	dialog.path[0] = '\0';
	SDL_ShowOpenFileDialog(NativeSetup_OnFileChosen, &dialog, NULL, filters, filterCount, NULL, false);

	int state;
	while ((state = SDL_GetAtomicInt(&dialog.state)) == NATIVE_SETUP_PICK_PENDING)
	{
		SDL_Event event;
		if (SDL_WaitEventTimeout(&event, 50) && (event.type == SDL_EVENT_QUIT))
		{
			// The dialog still owns its callback; let it finish first.
			continue;
		}
	}

	if (state == NATIVE_SETUP_PICK_CHOSEN)
	{
		SDL_strlcpy(path, dialog.path, pathSize);
	}

	return (enum NativeSetupPick)state;
}

// ---- Import window ----------------------------------------------------------

struct NativeSetupImport
{
	const char *source;
	const char *assetsDir;
	SDL_AtomicInt permille;
	SDL_AtomicInt finished;
	volatile int cancel;
	int ok;
	char err[512];
};

static void NativeSetup_OnProgress(void *user, uint64_t done, uint64_t total)
{
	struct NativeSetupImport *job = user;
	SDL_SetAtomicInt(&job->permille, (total != 0) ? (int)((done * 1000u) / total) : 0);
}

static int SDLCALL NativeSetup_ImportThread(void *user)
{
	struct NativeSetupImport *job = user;

	job->ok = DiscImport_Run(job->source, job->assetsDir, NativeSetup_OnProgress, job, &job->cancel, job->err, sizeof(job->err));
	SDL_SetAtomicInt(&job->finished, 1);
	return 0;
}

struct NativeSetupWindow
{
	SDL_Window *window;
	SDL_Renderer *renderer;
};

static void NativeSetup_OpenWindow(struct NativeSetupWindow *win)
{
	win->window = NULL;
	win->renderer = NULL;

	if (!SDL_CreateWindowAndRenderer(NATIVE_SETUP_TITLE, 640, 200, 0, &win->window, &win->renderer))
	{
		SDL_Log("[CTR Native] setup: no progress window: %s", SDL_GetError());
		return;
	}

	SDL_SetRenderVSync(win->renderer, 1);
}

static void NativeSetup_CloseWindow(struct NativeSetupWindow *win)
{
	if (win->renderer != NULL)
	{
		SDL_DestroyRenderer(win->renderer);
	}

	if (win->window != NULL)
	{
		SDL_DestroyWindow(win->window);
	}

	win->window = NULL;
	win->renderer = NULL;
}

// Draws up to three lines and an optional bar (permille < 0: no bar). Text is
// the 8x8 debug font, scaled so a line of ~40 characters fills the width.
static void NativeSetup_Draw(struct NativeSetupWindow *win, const char *line1, const char *line2, const char *line3, int permille)
{
	int width = 0;
	int height = 0;

	if ((win->renderer == NULL) || !SDL_GetCurrentRenderOutputSize(win->renderer, &width, &height) || (width <= 0) || (height <= 0))
	{
		return;
	}

	float scale = SDL_floorf((float)width / (44.0f * SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE));
	scale = SDL_clamp(scale, 1.0f, 8.0f);
	float w = (float)width / scale;
	float h = (float)height / scale;
	float margin = 16.0f;
	float lineHeight = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE + 8.0f;
	float top = SDL_max(margin, (h - 5.0f * lineHeight) * 0.5f);

	SDL_SetRenderScale(win->renderer, 1.0f, 1.0f);
	SDL_SetRenderDrawColor(win->renderer, 0x1b, 0x1f, 0x3b, 0xff);
	SDL_RenderClear(win->renderer);
	SDL_SetRenderScale(win->renderer, scale, scale);

	SDL_SetRenderDrawColor(win->renderer, 0xff, 0xd0, 0x40, 0xff);
	SDL_RenderDebugText(win->renderer, margin, top, line1);

	SDL_SetRenderDrawColor(win->renderer, 0xf0, 0xf0, 0xf0, 0xff);
	if (line2 != NULL)
	{
		SDL_RenderDebugText(win->renderer, margin, top + lineHeight, line2);
	}

	if (permille >= 0)
	{
		SDL_FRect frame = {margin, top + 2.0f * lineHeight, w - 2.0f * margin, lineHeight};
		SDL_FRect fill = {frame.x + 2.0f, frame.y + 2.0f, (frame.w - 4.0f) * (float)permille / 1000.0f, frame.h - 4.0f};
		SDL_RenderRect(win->renderer, &frame);
		SDL_SetRenderDrawColor(win->renderer, 0xff, 0x8a, 0x00, 0xff);
		SDL_RenderFillRect(win->renderer, &fill);
		SDL_SetRenderDrawColor(win->renderer, 0xa0, 0xa0, 0xb0, 0xff);
	}

	if (line3 != NULL)
	{
		SDL_RenderDebugText(win->renderer, margin, top + 3.5f * lineHeight, line3);
	}

	SDL_RenderPresent(win->renderer);
}

static int NativeSetup_IsCancelEvent(const SDL_Event *event)
{
	return (event->type == SDL_EVENT_QUIT) || (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) ||
	       ((event->type == SDL_EVENT_KEY_DOWN) && ((event->key.key == SDLK_ESCAPE) || (event->key.key == SDLK_AC_BACK)));
}

// Fallback when there is no file dialog (a Linux desktop without the XDG
// portal or zenity): the user drops the file onto the setup window.
static int NativeSetup_WaitForDrop(struct NativeSetupWindow *win, char *path, size_t pathSize)
{
	if (win->window == NULL)
	{
		return 0;
	}

	for (;;)
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (NativeSetup_IsCancelEvent(&event))
			{
				return 0;
			}

			if ((event.type == SDL_EVENT_DROP_FILE) && (event.drop.data != NULL))
			{
				return SDL_strlcpy(path, event.drop.data, pathSize) < pathSize;
			}
		}

		NativeSetup_Draw(win, "Drop your disc image here", "A .cue, .bin or .chd file.", NATIVE_SETUP_CANCEL_KEY ": quit", -1);
		SDL_Delay(16);
	}
}

static int NativeSetup_Import(struct NativeSetupWindow *win, const char *source, const char *assetsDir, char *err, size_t errSize)
{
	struct NativeSetupImport job;

	SDL_zero(job);
	job.source = source;
	job.assetsDir = assetsDir;

	SDL_Thread *thread = SDL_CreateThread(NativeSetup_ImportThread, "disc-import", &job);
	if (thread == NULL)
	{
		NativeSetup_ImportThread(&job);
	}

	int cancelling = 0;
	while (!SDL_GetAtomicInt(&job.finished))
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (NativeSetup_IsCancelEvent(&event))
			{
				// The worker stops at its next chunk and removes its partial file.
				job.cancel = 1;
				cancelling = 1;
			}
		}

		char percent[64];
		int permille = SDL_GetAtomicInt(&job.permille);
		SDL_snprintf(percent, sizeof(percent), "%d%%", permille / 10);
		NativeSetup_Draw(win, cancelling ? "Cancelling..." : "Installing the disc image...", percent, cancelling ? NULL : NATIVE_SETUP_CANCEL_KEY ": cancel", permille);
		SDL_Delay(16);
	}

	if (thread != NULL)
	{
		SDL_WaitThread(thread, NULL);
	}

	SDL_strlcpy(err, (job.err[0] != '\0') ? job.err : "The disc image could not be installed.", errSize);
	return job.ok ? 1 : (cancelling ? -1 : 0);
}

// ---- Setup ------------------------------------------------------------------


static int NativeSetup_TakeRequest(const char *baseDir)
{
	char path[NATIVE_SETUP_PATH_MAX];

	SDL_snprintf(path, sizeof(path), "%s/" NATIVE_SETUP_REQUEST_FILE, baseDir);
	if (!NativeSetup_IsFile(path))
	{
		return 0;
	}

	SDL_RemovePath(path);
	return 1;
}

static int NativeSetup_Wizard(const char *baseDir, int hasDisc)
{
	char assetsDir[NATIVE_SETUP_PATH_MAX];
	char source[NATIVE_SETUP_PATH_MAX];
	char err[512];
	struct NativeSetupWindow win = {NULL, NULL};
	int result = 0;

	SDL_snprintf(assetsDir, sizeof(assetsDir), "%s/assets", baseDir);

	const char *intro = hasDisc ? "Choose a different disc image to replace the one " NATIVE_PRODUCT_NAME " uses now, or Cancel to keep it."
	                            : NATIVE_PRODUCT_NAME " needs your own copy of Crash Team Racing for the PlayStation (North America, SCUS-94426).\n\n"
	                                                  "Choose a disc image: " NATIVE_SETUP_IMAGE_KINDS ". " NATIVE_SETUP_COPIED_TO
	                                                  ", so you only do this once.";
	const char *quitLabel = hasDisc ? "Cancel" : "Quit";

	if (!NativeSetup_Ask(SDL_MESSAGEBOX_INFORMATION, intro, "Choose disc image...", quitLabel))
	{
		return hasDisc;
	}

	for (;;)
	{
		enum NativeSetupPick pick = NativeSetup_PickFile(source, sizeof(source));

		if (pick == NATIVE_SETUP_PICK_UNAVAILABLE)
		{
			if (win.window == NULL)
			{
				NativeSetup_OpenWindow(&win);
			}
			pick = NativeSetup_WaitForDrop(&win, source, sizeof(source)) ? NATIVE_SETUP_PICK_CHOSEN : NATIVE_SETUP_PICK_CANCELLED;
		}

		if (pick != NATIVE_SETUP_PICK_CHOSEN)
		{
			// Cancel keeps the disc that is already installed.
			result = hasDisc;
			break;
		}

		if (win.window == NULL)
		{
			NativeSetup_OpenWindow(&win);
		}

		int imported = NativeSetup_Import(&win, source, assetsDir, err, sizeof(err));
		if (imported > 0)
		{
			result = 1;
			break;
		}

		if (imported < 0)
		{
			result = hasDisc;
			break;
		}

		SDL_HideWindow(win.window);
		if (!NativeSetup_Ask(SDL_MESSAGEBOX_WARNING, err, "Choose another...", quitLabel))
		{
			result = hasDisc;
			break;
		}
		SDL_ShowWindow(win.window);
	}

	NativeSetup_CloseWindow(&win);
	return result;
}

int NativeSetup_Run(char *baseDir, size_t baseDirSize, int force)
{
	char discPath[NATIVE_SETUP_PATH_MAX];
	char err[512];

	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		fprintf(stderr, "[CTR Native] setup: SDL_Init failed: %s\n", SDL_GetError());
		return 0;
	}

	int result = 0;
	if (!NativeSetup_GetDataDir(baseDir, baseDirSize))
	{
		NativeSetup_ShowFatal("Could not create the " NATIVE_PRODUCT_NAME " data folder.", NULL);
	}
	else
	{
		SDL_snprintf(discPath, sizeof(discPath), "%s/assets/" DISC_IMPORT_BIN_NAME, baseDir);
		int hasDisc = DiscImport_Check(discPath, err, sizeof(err));
		int requested = NativeSetup_TakeRequest(baseDir);

#if defined(_WIN32)
		// Shift at launch re-runs setup, like Option for the macOS app.
		force = force || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
#endif

		result = (hasDisc && !force && !requested) ? 1 : NativeSetup_Wizard(baseDir, hasDisc);
	}

	// NOTE: The game initialises SDL again for itself. Android keeps it up:
	// SDLActivity's surface and JNI state belong to the whole process.
#if !defined(SDL_PLATFORM_ANDROID)
	SDL_Quit();
#endif
	return result;
}
