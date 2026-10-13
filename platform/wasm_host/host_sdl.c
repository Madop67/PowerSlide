// Host-side SDL3 imports for the macOS wasm sandbox build.
//
// The guest is compiled against the same SDL3 headers, so plain-data structs
// (SDL_AudioSpec, most SDL_Event variants) have identical layouts on wasm32 and
// arm64. Opaque SDL objects cross the boundary as handles (see host_mem.h).

#include <string.h>

#include <SDL3/SDL.h>

#include "host_mem.h"

#define H2P(type, handle) ((type *)HostHandle_ToPtr(handle))

_Static_assert(sizeof(SDL_AudioSpec) == 12, "SDL_AudioSpec must match the wasm32 layout");
_Static_assert(sizeof(SDL_Event) == 128, "SDL_Event must match the wasm32 layout");

static const char *HostSDL_Str(u32 offset)
{
	if (offset == 0)
	{
		return NULL;
	}

	// NOTE: Bounds-check the start; the guest's strings are NUL-terminated in its own memory.
	return (const char *)HOST_G2H_SIZED(offset, 1);
}

// Resolves a guest file path the way the guest's own fopen() would: relative
// paths are relative to the guest's cwd, not the host process cwd.
static const char *HostSDL_Path(u32 offset, char *buf, size_t bufSize)
{
	const char *path = HostSDL_Str(offset);

	if ((path == NULL) || (path[0] == '/'))
	{
		return path;
	}

	const char *cwd = HostSDL_Str(w2c_ctr_ctr_guest_getcwd(&g_hostGuest));
	if (cwd == NULL)
	{
		return path;
	}

	snprintf(buf, bufSize, "%s/%s", cwd, path);
	return buf;
}

// ---- init / misc -----------------------------------------------------------

u32 w2c_env_SDL_Init(struct w2c_env *env, u32 flags)
{
	(void)env;
	return SDL_Init((SDL_InitFlags)flags);
}

u32 w2c_env_SDL_InitSubSystem(struct w2c_env *env, u32 flags)
{
	(void)env;
	return SDL_InitSubSystem((SDL_InitFlags)flags);
}

void w2c_env_SDL_QuitSubSystem(struct w2c_env *env, u32 flags)
{
	(void)env;
	SDL_QuitSubSystem((SDL_InitFlags)flags);
}

void w2c_env_SDL_Quit(struct w2c_env *env)
{
	(void)env;
	SDL_Quit();
}

u32 w2c_env_SDL_GetError(struct w2c_env *env)
{
	return HostMem_StringToGuest(env, SDL_GetError());
}

u32 w2c_env_SDL_GetBasePath(struct w2c_env *env)
{
	return HostMem_StringToGuest(env, SDL_GetBasePath());
}

u32 w2c_env_SDL_GetHint(struct w2c_env *env, u32 name)
{
	return HostMem_StringToGuest(env, SDL_GetHint(HostSDL_Str(name)));
}

u32 w2c_env_SDL_SetHint(struct w2c_env *env, u32 name, u32 value)
{
	(void)env;
	return SDL_SetHint(HostSDL_Str(name), HostSDL_Str(value));
}

void w2c_env_SDL_free(struct w2c_env *env, u32 ptr)
{
	(void)env;
	// NOTE: Arrays returned to the guest (SDL_GetGamepads) live in guest memory.
	HostMem_Free(ptr);
}

u64 w2c_env_SDL_GetPerformanceCounter(struct w2c_env *env)
{
	(void)env;
	return SDL_GetPerformanceCounter();
}

u64 w2c_env_SDL_GetPerformanceFrequency(struct w2c_env *env)
{
	(void)env;
	return SDL_GetPerformanceFrequency();
}

void w2c_env_SDL_DelayPrecise(struct w2c_env *env, u64 ns)
{
	(void)env;
	SDL_DelayPrecise(ns);
}

u32 w2c_env_SDL_ShowCursor(struct w2c_env *env)
{
	(void)env;
	return SDL_ShowCursor();
}

u32 w2c_env_SDL_HideCursor(struct w2c_env *env)
{
	(void)env;
	return SDL_HideCursor();
}

u64 w2c_env_SDL_GetTicks(struct w2c_env *env)
{
	(void)env;
	return SDL_GetTicks();
}

u32 w2c_env_SDL_GetVersion(struct w2c_env *env)
{
	(void)env;
	return (u32)SDL_GetVersion();
}

u32 w2c_env_SDL_GetCurrentVideoDriver(struct w2c_env *env)
{
	return HostMem_StringToGuest(env, SDL_GetCurrentVideoDriver());
}

u32 w2c_env_SDL_OpenURL(struct w2c_env *env, u32 url)
{
	(void)env;
	return SDL_OpenURL(HostSDL_Str(url));
}

// ---- events / keyboard -----------------------------------------------------

static u32 s_keyboardMirror;
static int s_keyboardMirrorCount;

static void HostSDL_RefreshKeyboardMirror(void)
{
	if (s_keyboardMirror == 0)
	{
		return;
	}

	int numKeys = 0;
	const bool *state = SDL_GetKeyboardState(&numKeys);
	if (numKeys > s_keyboardMirrorCount)
	{
		numKeys = s_keyboardMirrorCount;
	}

	memcpy(HOST_G2H_SIZED(s_keyboardMirror, (u64)s_keyboardMirrorCount), state, (size_t)numKeys);
}

u32 w2c_env_SDL_GetKeyboardState(struct w2c_env *env, u32 outNumKeys)
{
	(void)env;

	// NOTE: SDL returns a live array that the guest caches (native_input.c), so
	// mirror it once in guest memory and refresh it whenever events are pumped.
	if (s_keyboardMirror == 0)
	{
		int numKeys = 0;
		(void)SDL_GetKeyboardState(&numKeys);
		s_keyboardMirrorCount = numKeys;
		s_keyboardMirror = HostMem_Alloc((u32)numKeys);
		memset(HOST_G2H_SIZED(s_keyboardMirror, (u64)numKeys), 0, (size_t)numKeys);
	}

	HostSDL_RefreshKeyboardMirror();

	if (outNumKeys != 0)
	{
		int32_t count = s_keyboardMirrorCount;
		memcpy(HOST_G2H_SIZED(outNumKeys, 4), &count, 4);
	}

	return s_keyboardMirror;
}

void w2c_env_SDL_PumpEvents(struct w2c_env *env)
{
	(void)env;
	SDL_PumpEvents();
	HostSDL_RefreshKeyboardMirror();
}

// NOTE: Only event types with no pointer members are forwarded as-is; their
// layout is identical on wasm32 and arm64. Text input is translated by
// HostSDL_TranslateTextInput; drop/clipboard/user events are dropped.
static bool HostSDL_IsForwardedEvent(Uint32 type)
{
	if ((type >= SDL_EVENT_QUIT) && (type < SDL_EVENT_KEY_DOWN))
	{
		return true; // app, display and window events
	}

	switch (type)
	{
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
	case SDL_EVENT_KEYMAP_CHANGED:
	case SDL_EVENT_KEYBOARD_ADDED:
	case SDL_EVENT_KEYBOARD_REMOVED:
		return true;
	default:
		break;
	}

	if ((type >= SDL_EVENT_MOUSE_MOTION) && (type < SDL_EVENT_JOYSTICK_AXIS_MOTION))
	{
		return true;
	}

	if ((type >= SDL_EVENT_JOYSTICK_AXIS_MOTION) && (type < SDL_EVENT_FINGER_DOWN))
	{
		return true; // joystick and gamepad events
	}

	return false;
}

// SDL_TextInputEvent as laid out in wasm32 guest memory (4-byte pointers).
typedef struct GuestTextInputEvent
{
	Uint32 type;
	Uint32 reserved;
	Uint64 timestamp;
	SDL_WindowID windowID;
	Uint32 text;
} GuestTextInputEvent;

_Static_assert(sizeof(GuestTextInputEvent) == 24, "GuestTextInputEvent must match the wasm32 layout");

static u32 s_textInputBuffer;
static size_t s_textInputCapacity;

// NOTE: The guest consumes each text event before polling again (ImGui copies
// the characters), so one reusable guest buffer holds the current text.
static void HostSDL_TranslateTextInput(const SDL_TextInputEvent *text, u32 eventOut)
{
	const char *str = (text->text != NULL) ? text->text : "";
	size_t len = strlen(str) + 1;

	if (len > s_textInputCapacity)
	{
		HostMem_Free(s_textInputBuffer);
		s_textInputCapacity = (len < 64) ? 64 : len;
		s_textInputBuffer = HostMem_Alloc((u32)s_textInputCapacity);
	}

	memcpy(HOST_G2H_SIZED(s_textInputBuffer, len), str, len);

	GuestTextInputEvent guest = {0};
	guest.type = text->type;
	guest.timestamp = text->timestamp;
	guest.windowID = text->windowID;
	guest.text = s_textInputBuffer;

	uint8_t *out = HOST_G2H_SIZED(eventOut, sizeof(SDL_Event));
	memset(out, 0, sizeof(SDL_Event));
	memcpy(out, &guest, sizeof(guest));
}

u32 w2c_env_SDL_PollEvent(struct w2c_env *env, u32 eventOut)
{
	(void)env;
	SDL_Event event;

	if (eventOut == 0)
	{
		return SDL_PollEvent(NULL);
	}

	while (SDL_PollEvent(&event))
	{
		if (event.type == SDL_EVENT_TEXT_INPUT)
		{
			HostSDL_RefreshKeyboardMirror();
			HostSDL_TranslateTextInput(&event.text, eventOut);
			return 1;
		}

		if (!HostSDL_IsForwardedEvent(event.type))
		{
			continue;
		}

		HostSDL_RefreshKeyboardMirror();
		memcpy(HOST_G2H_SIZED(eventOut, sizeof(SDL_Event)), &event, sizeof(SDL_Event));
		return 1;
	}

	HostSDL_RefreshKeyboardMirror();
	return 0;
}

u32 w2c_env_SDL_GetScancodeName(struct w2c_env *env, u32 scancode)
{
	return HostMem_StringToGuest(env, SDL_GetScancodeName((SDL_Scancode)scancode));
}

u32 w2c_env_SDL_GetScancodeFromName(struct w2c_env *env, u32 name)
{
	(void)env;
	return (u32)SDL_GetScancodeFromName(HostSDL_Str(name));
}

// ---- window / GL context ---------------------------------------------------

u32 w2c_env_SDL_CreateWindow(struct w2c_env *env, u32 title, u32 w, u32 h, u64 flags)
{
	(void)env;
	return HostHandle_FromPtr(SDL_CreateWindow(HostSDL_Str(title), (int)w, (int)h, (SDL_WindowFlags)flags));
}

void w2c_env_SDL_DestroyWindow(struct w2c_env *env, u32 window)
{
	(void)env;
	SDL_DestroyWindow(H2P(SDL_Window, window));
	HostHandle_Release(window);
}

u64 w2c_env_SDL_GetWindowFlags(struct w2c_env *env, u32 window)
{
	(void)env;
	return SDL_GetWindowFlags(H2P(SDL_Window, window));
}

u32 w2c_env_SDL_GetWindowSize(struct w2c_env *env, u32 window, u32 outW, u32 outH)
{
	(void)env;
	int w = 0;
	int h = 0;
	bool ok = SDL_GetWindowSize(H2P(SDL_Window, window), &w, &h);

	if (outW != 0)
	{
		memcpy(HOST_G2H_SIZED(outW, 4), &w, 4);
	}

	if (outH != 0)
	{
		memcpy(HOST_G2H_SIZED(outH, 4), &h, 4);
	}

	return ok;
}

u32 w2c_env_SDL_SetWindowFullscreen(struct w2c_env *env, u32 window, u32 fullscreen)
{
	(void)env;
	return SDL_SetWindowFullscreen(H2P(SDL_Window, window), fullscreen != 0);
}

u32 w2c_env_SDL_GL_SetAttribute(struct w2c_env *env, u32 attr, u32 value)
{
	(void)env;

	// NOTE: macOS only provides core profiles 3.2+ as forward-compatible contexts.
	if (((SDL_GLAttr)attr == SDL_GL_CONTEXT_PROFILE_MASK) && ((int)value == SDL_GL_CONTEXT_PROFILE_CORE))
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	}

	return SDL_GL_SetAttribute((SDL_GLAttr)attr, (int)value);
}

u32 w2c_env_SDL_GL_CreateContext(struct w2c_env *env, u32 window)
{
	(void)env;
	return HostHandle_FromPtr(SDL_GL_CreateContext(H2P(SDL_Window, window)));
}

u32 w2c_env_SDL_GL_ExtensionSupported(struct w2c_env *env, u32 extension)
{
	(void)env;
	return SDL_GL_ExtensionSupported(HostSDL_Str(extension));
}

u32 w2c_env_SDL_GL_SetSwapInterval(struct w2c_env *env, u32 interval)
{
	(void)env;
	return SDL_GL_SetSwapInterval((int)interval);
}

u32 w2c_env_SDL_GL_SwapWindow(struct w2c_env *env, u32 window)
{
	(void)env;
	return SDL_GL_SwapWindow(H2P(SDL_Window, window));
}

u32 w2c_env_SDL_SetWindowSize(struct w2c_env *env, u32 window, u32 w, u32 h)
{
	(void)env;
	return SDL_SetWindowSize(H2P(SDL_Window, window), (int)w, (int)h);
}

u32 w2c_env_SDL_SetWindowPosition(struct w2c_env *env, u32 window, u32 x, u32 y)
{
	(void)env;
	return SDL_SetWindowPosition(H2P(SDL_Window, window), (int)x, (int)y);
}

u32 w2c_env_SDL_SyncWindow(struct w2c_env *env, u32 window)
{
	(void)env;
	return SDL_SyncWindow(H2P(SDL_Window, window));
}

u32 w2c_env_SDL_GL_GetCurrentContext(struct w2c_env *env)
{
	(void)env;
	return HostHandle_FromPtr(SDL_GL_GetCurrentContext());
}

static void HostSDL_WriteInt2(u32 outA, u32 outB, int a, int b)
{
	if (outA != 0)
	{
		memcpy(HOST_G2H_SIZED(outA, 4), &a, 4);
	}

	if (outB != 0)
	{
		memcpy(HOST_G2H_SIZED(outB, 4), &b, 4);
	}
}

u32 w2c_env_SDL_GetWindowPosition(struct w2c_env *env, u32 window, u32 outX, u32 outY)
{
	(void)env;
	int x = 0;
	int y = 0;
	bool ok = SDL_GetWindowPosition(H2P(SDL_Window, window), &x, &y);

	HostSDL_WriteInt2(outX, outY, x, y);
	return ok;
}

u32 w2c_env_SDL_GetWindowSizeInPixels(struct w2c_env *env, u32 window, u32 outW, u32 outH)
{
	(void)env;
	int w = 0;
	int h = 0;
	bool ok = SDL_GetWindowSizeInPixels(H2P(SDL_Window, window), &w, &h);

	HostSDL_WriteInt2(outW, outH, w, h);
	return ok;
}

f32 w2c_env_SDL_GetWindowDisplayScale(struct w2c_env *env, u32 window)
{
	(void)env;
	return SDL_GetWindowDisplayScale(H2P(SDL_Window, window));
}

u32 w2c_env_SDL_GetWindowID(struct w2c_env *env, u32 window)
{
	(void)env;
	return SDL_GetWindowID(H2P(SDL_Window, window));
}

// NOTE: HostHandle_FromPtr reuses an existing handle, so these return the same
// handle the guest got from SDL_CreateWindow.
u32 w2c_env_SDL_GetWindowFromID(struct w2c_env *env, u32 id)
{
	(void)env;
	return HostHandle_FromPtr(SDL_GetWindowFromID((SDL_WindowID)id));
}

u32 w2c_env_SDL_GetKeyboardFocus(struct w2c_env *env)
{
	(void)env;
	return HostHandle_FromPtr(SDL_GetKeyboardFocus());
}

u32 w2c_env_SDL_GetWindowRelativeMouseMode(struct w2c_env *env, u32 window)
{
	(void)env;
	return SDL_GetWindowRelativeMouseMode(H2P(SDL_Window, window));
}

u32 w2c_env_SDL_PushEvent(struct w2c_env *env, u32 event)
{
	(void)env;
	SDL_Event hostEvent;

	memcpy(&hostEvent, HOST_G2H_SIZED(event, sizeof(SDL_Event)), sizeof(SDL_Event));

	// NOTE: Events with pointer members would carry guest offsets; none are pushed today.
	if (!HostSDL_IsForwardedEvent(hostEvent.type))
	{
		HostMem_Fatal("SDL_PushEvent: unsupported event type 0x%x", hostEvent.type);
	}

	return SDL_PushEvent(&hostEvent);
}

// ---- mouse / cursor / clipboard / text input (settings UI) ------------------

u32 w2c_env_SDL_CaptureMouse(struct w2c_env *env, u32 enabled)
{
	(void)env;
	return SDL_CaptureMouse(enabled != 0);
}

u32 w2c_env_SDL_GetGlobalMouseState(struct w2c_env *env, u32 outX, u32 outY)
{
	(void)env;
	float x = 0.0f;
	float y = 0.0f;
	SDL_MouseButtonFlags buttons = SDL_GetGlobalMouseState(&x, &y);

	if (outX != 0)
	{
		memcpy(HOST_G2H_SIZED(outX, 4), &x, 4);
	}

	if (outY != 0)
	{
		memcpy(HOST_G2H_SIZED(outY, 4), &y, 4);
	}

	return buttons;
}

void w2c_env_SDL_WarpMouseInWindow(struct w2c_env *env, u32 window, f32 x, f32 y)
{
	(void)env;
	SDL_WarpMouseInWindow(H2P(SDL_Window, window), x, y);
}

u32 w2c_env_SDL_CreateSystemCursor(struct w2c_env *env, u32 id)
{
	(void)env;
	return HostHandle_FromPtr(SDL_CreateSystemCursor((SDL_SystemCursor)id));
}

u32 w2c_env_SDL_SetCursor(struct w2c_env *env, u32 cursor)
{
	(void)env;
	return SDL_SetCursor(H2P(SDL_Cursor, cursor));
}

void w2c_env_SDL_DestroyCursor(struct w2c_env *env, u32 cursor)
{
	(void)env;
	SDL_DestroyCursor(H2P(SDL_Cursor, cursor));
	HostHandle_Release(cursor);
}

u32 w2c_env_SDL_GetClipboardText(struct w2c_env *env)
{
	(void)env;
	char *text = SDL_GetClipboardText();

	if (text == NULL)
	{
		return 0;
	}

	// NOTE: Copied into guest memory; the guest releases it with SDL_free.
	u32 len = (u32)strlen(text) + 1;
	u32 guestText = HostMem_Alloc(len);
	memcpy(HOST_G2H_SIZED(guestText, len), text, len);
	SDL_free(text);
	return guestText;
}

u32 w2c_env_SDL_SetClipboardText(struct w2c_env *env, u32 text)
{
	(void)env;
	return SDL_SetClipboardText(HostSDL_Str(text));
}

u32 w2c_env_SDL_StartTextInput(struct w2c_env *env, u32 window)
{
	(void)env;
	return SDL_StartTextInput(H2P(SDL_Window, window));
}

u32 w2c_env_SDL_StopTextInput(struct w2c_env *env, u32 window)
{
	(void)env;
	return SDL_StopTextInput(H2P(SDL_Window, window));
}

u32 w2c_env_SDL_TextInputActive(struct w2c_env *env, u32 window)
{
	(void)env;
	return SDL_TextInputActive(H2P(SDL_Window, window));
}

u32 w2c_env_SDL_SetTextInputArea(struct w2c_env *env, u32 window, u32 rect, u32 cursor)
{
	(void)env;
	return SDL_SetTextInputArea(H2P(SDL_Window, window), HOST_G2H_SIZED(rect, sizeof(SDL_Rect)), (int)cursor);
}

// ---- surfaces (screenshots) ------------------------------------------------

u32 w2c_env_SDL_CreateSurfaceFrom(struct w2c_env *env, u32 w, u32 h, u32 format, u32 pixels, u32 pitch)
{
	(void)env;
	void *hostPixels = HOST_G2H_SIZED(pixels, (u64)h * pitch);
	return HostHandle_FromPtr(SDL_CreateSurfaceFrom((int)w, (int)h, (SDL_PixelFormat)format, hostPixels, (int)pitch));
}

u32 w2c_env_SDL_SaveBMP(struct w2c_env *env, u32 surface, u32 file)
{
	(void)env;
	char path[4096];
	return SDL_SaveBMP(H2P(SDL_Surface, surface), HostSDL_Path(file, path, sizeof(path)));
}

void w2c_env_SDL_DestroySurface(struct w2c_env *env, u32 surface)
{
	(void)env;
	SDL_DestroySurface(H2P(SDL_Surface, surface));
	HostHandle_Release(surface);
}

// ---- audio -----------------------------------------------------------------

u32 w2c_env_SDL_OpenAudioDeviceStream(struct w2c_env *env, u32 devid, u32 spec, u32 callback, u32 userdata)
{
	(void)env;
	(void)userdata;

	// NOTE: SDL would invoke the callback on its audio thread, which must never
	// re-enter the single-threaded guest. native_audio.c pumps the stream instead.
	if (callback != 0)
	{
		HostMem_Fatal("SDL_OpenAudioDeviceStream: guest callbacks are not supported");
	}

	const SDL_AudioSpec *hostSpec = HOST_G2H_SIZED(spec, sizeof(SDL_AudioSpec));
	return HostHandle_FromPtr(SDL_OpenAudioDeviceStream((SDL_AudioDeviceID)devid, hostSpec, NULL, NULL));
}

u32 w2c_env_SDL_GetAudioStreamDevice(struct w2c_env *env, u32 stream)
{
	(void)env;
	return SDL_GetAudioStreamDevice(H2P(SDL_AudioStream, stream));
}

u32 w2c_env_SDL_GetAudioDeviceFormat(struct w2c_env *env, u32 devid, u32 spec, u32 sampleFrames)
{
	(void)env;
	SDL_AudioSpec hostSpec;
	int frames = 0;
	bool ok = SDL_GetAudioDeviceFormat((SDL_AudioDeviceID)devid, &hostSpec, &frames);

	if (ok && (spec != 0))
	{
		memcpy(HOST_G2H_SIZED(spec, sizeof(hostSpec)), &hostSpec, sizeof(hostSpec));
	}

	if (ok && (sampleFrames != 0))
	{
		memcpy(HOST_G2H_SIZED(sampleFrames, 4), &frames, 4);
	}

	return ok;
}

u32 w2c_env_SDL_GetAudioStreamFormat(struct w2c_env *env, u32 stream, u32 srcSpec, u32 dstSpec)
{
	(void)env;
	SDL_AudioSpec src;
	SDL_AudioSpec dst;
	bool ok = SDL_GetAudioStreamFormat(H2P(SDL_AudioStream, stream), &src, &dst);

	if (ok && (srcSpec != 0))
	{
		memcpy(HOST_G2H_SIZED(srcSpec, sizeof(src)), &src, sizeof(src));
	}

	if (ok && (dstSpec != 0))
	{
		memcpy(HOST_G2H_SIZED(dstSpec, sizeof(dst)), &dst, sizeof(dst));
	}

	return ok;
}

u32 w2c_env_SDL_GetAudioStreamQueued(struct w2c_env *env, u32 stream)
{
	(void)env;
	return (u32)SDL_GetAudioStreamQueued(H2P(SDL_AudioStream, stream));
}

u32 w2c_env_SDL_PutAudioStreamData(struct w2c_env *env, u32 stream, u32 buf, u32 len)
{
	(void)env;
	return SDL_PutAudioStreamData(H2P(SDL_AudioStream, stream), HOST_G2H_SIZED(buf, len), (int)len);
}

u32 w2c_env_SDL_ClearAudioStream(struct w2c_env *env, u32 stream)
{
	(void)env;
	return SDL_ClearAudioStream(H2P(SDL_AudioStream, stream));
}

u32 w2c_env_SDL_ResumeAudioStreamDevice(struct w2c_env *env, u32 stream)
{
	(void)env;
	return SDL_ResumeAudioStreamDevice(H2P(SDL_AudioStream, stream));
}

u32 w2c_env_SDL_LockAudioStream(struct w2c_env *env, u32 stream)
{
	(void)env;
	return SDL_LockAudioStream(H2P(SDL_AudioStream, stream));
}

u32 w2c_env_SDL_UnlockAudioStream(struct w2c_env *env, u32 stream)
{
	(void)env;
	return SDL_UnlockAudioStream(H2P(SDL_AudioStream, stream));
}

void w2c_env_SDL_DestroyAudioStream(struct w2c_env *env, u32 stream)
{
	(void)env;
	SDL_DestroyAudioStream(H2P(SDL_AudioStream, stream));
	HostHandle_Release(stream);
}

u32 w2c_env_SDL_GetCurrentAudioDriver(struct w2c_env *env)
{
	return HostMem_StringToGuest(env, SDL_GetCurrentAudioDriver());
}

// ---- gamepads --------------------------------------------------------------

u32 w2c_env_SDL_AddGamepadMappingsFromFile(struct w2c_env *env, u32 file)
{
	(void)env;
	char path[4096];
	return (u32)SDL_AddGamepadMappingsFromFile(HostSDL_Path(file, path, sizeof(path)));
}

u32 w2c_env_SDL_GetGamepads(struct w2c_env *env, u32 outCount)
{
	(void)env;
	int count = 0;
	SDL_JoystickID *ids = SDL_GetGamepads(&count);

	if (outCount != 0)
	{
		memcpy(HOST_G2H_SIZED(outCount, 4), &count, 4);
	}

	if (ids == NULL)
	{
		return 0;
	}

	// NOTE: Copied into guest memory (zero-terminated like SDL's); the guest
	// releases it with SDL_free, which frees from guest memory.
	u32 bytes = (u32)(count + 1) * (u32)sizeof(SDL_JoystickID);
	u32 guestIds = HostMem_Alloc(bytes);
	memcpy(HOST_G2H_SIZED(guestIds, bytes), ids, bytes);
	SDL_free(ids);
	return guestIds;
}

u32 w2c_env_SDL_IsGamepad(struct w2c_env *env, u32 id)
{
	(void)env;
	return SDL_IsGamepad((SDL_JoystickID)id);
}

u32 w2c_env_SDL_OpenGamepad(struct w2c_env *env, u32 id)
{
	(void)env;
	return HostHandle_FromPtr(SDL_OpenGamepad((SDL_JoystickID)id));
}

void w2c_env_SDL_CloseGamepad(struct w2c_env *env, u32 gamepad)
{
	(void)env;
	SDL_CloseGamepad(H2P(SDL_Gamepad, gamepad));
	HostHandle_Release(gamepad);
}

u32 w2c_env_SDL_GamepadConnected(struct w2c_env *env, u32 gamepad)
{
	(void)env;
	return SDL_GamepadConnected(H2P(SDL_Gamepad, gamepad));
}

u32 w2c_env_SDL_GetGamepadJoystick(struct w2c_env *env, u32 gamepad)
{
	(void)env;
	return HostHandle_FromPtr(SDL_GetGamepadJoystick(H2P(SDL_Gamepad, gamepad)));
}

u32 w2c_env_SDL_GetJoystickID(struct w2c_env *env, u32 joystick)
{
	(void)env;
	return SDL_GetJoystickID(H2P(SDL_Joystick, joystick));
}

u32 w2c_env_SDL_GetGamepadAxis(struct w2c_env *env, u32 gamepad, u32 axis)
{
	(void)env;
	return (u32)(int32_t)SDL_GetGamepadAxis(H2P(SDL_Gamepad, gamepad), (SDL_GamepadAxis)axis);
}

u32 w2c_env_SDL_GetGamepadButton(struct w2c_env *env, u32 gamepad, u32 button)
{
	(void)env;
	return SDL_GetGamepadButton(H2P(SDL_Gamepad, gamepad), (SDL_GamepadButton)button);
}

u32 w2c_env_SDL_RumbleGamepad(struct w2c_env *env, u32 gamepad, u32 low, u32 high, u32 durationMs)
{
	(void)env;
	return SDL_RumbleGamepad(H2P(SDL_Gamepad, gamepad), (Uint16)low, (Uint16)high, durationMs);
}

u32 w2c_env_SDL_GetGamepadFromID(struct w2c_env *env, u32 id)
{
	(void)env;
	return HostHandle_FromPtr(SDL_GetGamepadFromID((SDL_JoystickID)id));
}

u32 w2c_env_SDL_GetGamepadName(struct w2c_env *env, u32 gamepad)
{
	return HostMem_StringToGuest(env, SDL_GetGamepadName(H2P(SDL_Gamepad, gamepad)));
}

u32 w2c_env_SDL_GetGamepadType(struct w2c_env *env, u32 gamepad)
{
	(void)env;
	return (u32)SDL_GetGamepadType(H2P(SDL_Gamepad, gamepad));
}

u32 w2c_env_SDL_GetGamepadStringForButton(struct w2c_env *env, u32 button)
{
	return HostMem_StringToGuest(env, SDL_GetGamepadStringForButton((SDL_GamepadButton)button));
}

u32 w2c_env_SDL_GetGamepadButtonFromString(struct w2c_env *env, u32 str)
{
	(void)env;
	return (u32)SDL_GetGamepadButtonFromString(HostSDL_Str(str));
}

u32 w2c_env_SDL_GetGamepadStringForAxis(struct w2c_env *env, u32 axis)
{
	return HostMem_StringToGuest(env, SDL_GetGamepadStringForAxis((SDL_GamepadAxis)axis));
}

u32 w2c_env_SDL_GetGamepadAxisFromString(struct w2c_env *env, u32 str)
{
	(void)env;
	return (u32)SDL_GetGamepadAxisFromString(HostSDL_Str(str));
}

u32 w2c_env_SDL_GetGamepadButtonLabelForType(struct w2c_env *env, u32 type, u32 button)
{
	(void)env;
	return (u32)SDL_GetGamepadButtonLabelForType((SDL_GamepadType)type, (SDL_GamepadButton)button);
}
