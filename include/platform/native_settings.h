#ifndef NATIVE_SETTINGS_H
#define NATIVE_SETTINGS_H

// NOTE: Shared with the C++ UI layer; keep to plain C types.

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum NativeDisplayMode
{
	NATIVE_DISPLAY_WINDOWED = 0,
	NATIVE_DISPLAY_FULLSCREEN = 1,
	NATIVE_DISPLAY_COUNT
};

// Saved by name, so the numeric order is free to change.
enum NativeAspectMode
{
	NATIVE_ASPECT_ORIGINAL = 0,
	// Wide modes show more of the world: wider 3D view and culling, presented at the
	// chosen ratio (see NativeSettings_WidescreenActive).
	NATIVE_ASPECT_WIDESCREEN, // 16:9
	NATIVE_ASPECT_ULTRAWIDE,  // 21:9
	NATIVE_ASPECT_CUSTOM,     // aspectCustomWidth:aspectCustomHeight
	NATIVE_ASPECT_STRETCH,
	NATIVE_ASPECT_COUNT
};

// Custom aspect ratios are kept between 4:3 and 32:9.
#define NATIVE_ASPECT_CUSTOM_MAX_TERM 16384

#define NATIVE_RENDER_SCALE_MAX 8
// Custom internal resolution, in percent of the original.
#define NATIVE_RENDER_SCALE_PERCENT_MIN 100
#define NATIVE_RENDER_SCALE_PERCENT_MAX 1600

// Frames per second shown. The game always runs its logic at 30; above that,
// in-between frames are interpolated (see native_interp.h), below it some frames
// are not presented.
#define NATIVE_FRAME_RATE_ORIGINAL 30
#define NATIVE_FRAME_RATE_MIN      20
#define NATIVE_FRAME_RATE_MAX      240
// Highest refresh rate "match display" follows.
#define NATIVE_FRAME_RATE_DISPLAY_MAX 360

// Widescreen race HUD placement.
enum NativeHudPosition
{
	NATIVE_HUD_POSITION_EDGES = 0,
	NATIVE_HUD_POSITION_CENTRE,
	NATIVE_HUD_POSITION_COUNT
};

enum NativeScaleFilter
{
	NATIVE_SCALE_FILTER_SHARP = 0,
	NATIVE_SCALE_FILTER_SMOOTH = 1,
	NATIVE_SCALE_FILTER_COUNT
};

struct NativeVideoSettings
{
	int displayMode;
	// Windowed client size; 0 means "use the built-in default".
	int windowWidth;
	int windowHeight;
	int aspectMode;
	int aspectCustomWidth;
	int aspectCustomHeight;
	int hudPosition;
	// Internal resolution multiplier for the main render target (1 = original).
	int renderScale;
	// When set, renderScalePercent (a fractional multiplier) replaces renderScale.
	int renderScaleCustom;
	int renderScalePercent;
	int integerScaling;
	int scaleFilter;
	int textureFiltering;
	// Full-colour pause background instead of retail's sepia screenshot.
	int pauseFullColor;
	int frameRate;
	// Follow the display's refresh rate instead of frameRate.
	int frameRateMatchDisplay;
	int showFps;
};

// PS1 pad buttons, in the order the input menu lists them. Saved by name.
enum NativePadButton
{
	NATIVE_PAD_CROSS = 0,
	NATIVE_PAD_CIRCLE,
	NATIVE_PAD_SQUARE,
	NATIVE_PAD_TRIANGLE,
	NATIVE_PAD_L1,
	NATIVE_PAD_R1,
	NATIVE_PAD_L2,
	NATIVE_PAD_R2,
	NATIVE_PAD_UP,
	NATIVE_PAD_DOWN,
	NATIVE_PAD_LEFT,
	NATIVE_PAD_RIGHT,
	NATIVE_PAD_START,
	NATIVE_PAD_SELECT,
	NATIVE_PAD_L3,
	NATIVE_PAD_R3,
	NATIVE_PAD_BUTTON_COUNT
};

// Keys per PS1 button (primary and alternate).
#define NATIVE_KEY_BINDINGS      2
// Gamepad binding encoding: an SDL_GamepadButton, or NATIVE_PAD_BIND_AXIS | SDL_GamepadAxis,
// with NATIVE_PAD_BIND_NEGATIVE for the axis' negative direction.
#define NATIVE_PAD_BIND_NONE     (-1)
#define NATIVE_PAD_BIND_AXIS     0x4000
#define NATIVE_PAD_BIND_NEGATIVE 0x8000
#define NATIVE_INPUT_PLAYERS     4
// "vvvv:pppp" USB vendor:product id of a remembered controller.
#define NATIVE_CONTROLLER_ID_MAX   16
#define NATIVE_CONTROLLER_NAME_MAX 64

// One player's controls. Each player has its own keys (one keyboard can drive several
// players) and controller layout; the player's controller and keys share the pad.
struct NativePlayerInputSettings
{
	// Controller remembered for this player; it rejoins this player when it reconnects. "" = none.
	char controllerId[NATIVE_CONTROLLER_ID_MAX];
	char controllerName[NATIVE_CONTROLLER_NAME_MAX];
	// SDL scancodes; 0 means unbound.
	int keyboard[NATIVE_PAD_BUTTON_COUNT][NATIVE_KEY_BINDINGS];
	int gamepad[NATIVE_PAD_BUTTON_COUNT];
	// Radial stick deadzone, percent (0 = raw, as retail native).
	int stickDeadzone;
	// How far a trigger or stick bound to a button must move to press it, percent.
	int analogPressPoint;
	int rumble;
	// Percent.
	int rumbleStrength;
};

struct NativeInputSettings
{
	struct NativePlayerInputSettings players[NATIVE_INPUT_PLAYERS];
};

// game.fast_boot. Saved by name.
enum NativeFastBoot
{
	NATIVE_FAST_BOOT_OFF = 0,
	// Skip the SCEA and copyright screens and the Naughty Dog crate; boot into the title.
	NATIVE_FAST_BOOT_TITLE,
	// Also skip the title animation; boot into the main menu.
	NATIVE_FAST_BOOT_MENU,
	NATIVE_FAST_BOOT_COUNT
};

// Shown as the menu's Tweaks tab; the settings file keeps the [game] section name.
struct NativeGameSettings
{
	int fastBoot;
	// Every character is selectable, without changing the save.
	int unlockAllCharacters;
	// Retail's turbo cheat: start each race with turbos that never run out.
	int unlimitedTurbo;
	// Retail's wumpa cheat: start each race with full wumpa that never drops.
	int unlimitedWumpa;
	// The rest of retail's cheat codes, as toggles (see native_tweaks.h).
	int unlimitedMasks;
	int unlimitedBombs;
	int superEngine;
	int permanentInvisibility;
	int icyTracks;
	int superTurboPads;
	int oneLapRace;
	int superHard;
	int adventureDifficulty;
	int turboCounter;
};

struct NativeGlobalSettings
{
	// Runs every preset setting at its retail value (see NativeSettings_RefreshPs1Mode);
	// the chosen values are kept and come back when it is turned off.
	int ps1Mode;
};

struct NativeSettings
{
	struct NativeGlobalSettings global;
	struct NativeGameSettings game;
	struct NativeVideoSettings video;
	struct NativeInputSettings input;
};

extern struct NativeSettings g_settings;

// CVar registry. Every setting is a CVar named "<section>.<key>", as the settings file
// groups it: "video.render_scale", "input.player1.stick_deadzone". The settings file and
// --set on the command line read and write g_settings through it; the menu edits
// g_settings directly and saves through it.
enum NativeCVarType
{
	NATIVE_CVAR_INT,
	NATIVE_CVAR_BOOL,
	NATIVE_CVAR_ENUM,
	// SDL scancode, saved by key name ("Left Shift"); 0 = "none".
	NATIVE_CVAR_SCANCODE,
	// Gamepad binding (see NATIVE_PAD_BIND_*), saved by SDL name ("a", "+lefttrigger").
	NATIVE_CVAR_PAD_BINDING,
	// char[maxValue], saved quoted.
	NATIVE_CVAR_STRING,
};

struct NativeCVar
{
	const char *section;
	const char *key;
	enum NativeCVarType type;
	// Where the value lives in struct NativeSettings.
	size_t offset;
	int minValue;
	int maxValue;
	// Enum value names, indexed by value.
	const char *const *names;
	// NATIVE_CVAR_FLAG_*.
	int flags;
};

// Part of a preset, and reset by PS1 mode: game behaviour and picture options. Controls,
// the window and the FPS counter are not.
#define NATIVE_CVAR_FLAG_PRESET 1

int NativeCVar_Count(void);
const struct NativeCVar *NativeCVar_At(int index);
// Finds a CVar by its full name ("video.aspect_ratio"). NULL if there is none.
const struct NativeCVar *NativeCVar_Find(const char *name);
// Sets a CVar in g_settings from text written as in the settings file (3, true,
// "widescreen"); names may also be unquoted. Returns 0, changing nothing, if the text is invalid.
int NativeCVar_SetFromText(const struct NativeCVar *cvar, const char *text);
// Writes a CVar's current value as the settings file does.
void NativeCVar_Format(const struct NativeCVar *cvar, char *out, size_t outSize);
// Applies every "--set <name>=<value>" argument over the loaded settings. They are saved
// like any other change if the menu later saves. Returns 0 if one is invalid.
int NativeSettings_ApplyOverrides(int argc, char **argv);

void NativeSettings_SetDefaults(struct NativeSettings *settings);
// Sets every preset setting to its retail value.
void NativeSettings_SetRetail(struct NativeSettings *settings);
// Brings the preset settings in line with global.ps1Mode after it changes: turning it on keeps
// the chosen values aside and runs retail values; turning it off restores them. Saving always
// writes the chosen values. The caller applies and saves.
void NativeSettings_RefreshPs1Mode(void);
void NativeSettings_SetInputDefaults(struct NativeInputSettings *input);
// Player 1 gets the classic keyboard layout; other players start with no keys.
void NativeSettings_SetPlayerInputDefaults(struct NativePlayerInputSettings *player, int playerIndex);
// Stable lower-case name of a pad button ("cross", "l2", "dpad_up"); used as a settings key.
const char *NativeSettings_PadButtonKey(int button);
// Loads <baseDir>/ctr-native.toml over the defaults. Returns 1 if a file was read.
int NativeSettings_Load(const char *baseDir);
// Uses the defaults (retail behaviour) and never writes the settings file (--vanilla).
void NativeSettings_LoadVanilla(void);
// Writes the current settings back to the file chosen by NativeSettings_Load.
int NativeSettings_Save(void);
const char *NativeSettings_GetPath(void);

// Presets: named sets of the preset settings (NATIVE_CVAR_FLAG_PRESET). Two are built in;
// custom ones are saved to ctr-native-presets.toml next to the settings file.
#define NATIVE_PRESET_NAME_MAX   32
#define NATIVE_PRESET_CUSTOM_MAX 16
enum NativeBuiltinPreset
{
	NATIVE_PRESET_VANILLA = 0,
	NATIVE_PRESET_ENHANCED,
	NATIVE_PRESET_BUILTIN_COUNT
};

int NativePreset_Count(void);
const char *NativePreset_Name(int index);
int NativePreset_IsCustom(int index);
// Index of the preset with this name (case-insensitive), or -1.
int NativePreset_Find(const char *name);
// 1 if g_settings' preset settings equal the preset's.
int NativePreset_Matches(int index);
// Copies the preset's settings into g_settings. The caller applies and saves.
void NativePreset_Apply(int index);
// Saves g_settings' preset settings as a custom preset, replacing one with the same name.
// Returns its index, or -1 if the name is empty, built in, or there is no room.
int NativePreset_SaveCustom(const char *name);
void NativePreset_DeleteCustom(int index);

// Applies g_settings.video to the window and renderer (implemented by the platform layer).
void Platform_ApplyVideoSettings(void);
// Applies g_settings.input to the input layer.
void Platform_ApplyInputSettings(void);

// Picture aspect ratio of a fixed-ratio mode as width:height. Returns 0 for stretch.
static inline int NativeSettings_AspectRatio(int *width, int *height)
{
	const struct NativeVideoSettings *video = &g_settings.video;
	switch (video->aspectMode)
	{
	case NATIVE_ASPECT_WIDESCREEN:
		*width = 16;
		*height = 9;
		return 1;
	case NATIVE_ASPECT_ULTRAWIDE:
		*width = 21;
		*height = 9;
		return 1;
	case NATIVE_ASPECT_CUSTOM:
		// Clamped to 4:3 .. 32:9.
		*width = (video->aspectCustomWidth > 0) ? video->aspectCustomWidth : 16;
		*height = (video->aspectCustomHeight > 0) ? video->aspectCustomHeight : 9;
		if (*width * 3 < *height * 4)
		{
			*width = 4;
			*height = 3;
		}
		else if (*width * 9 > *height * 32)
		{
			*width = 32;
			*height = 9;
		}
		return 1;
	case NATIVE_ASPECT_STRETCH:
		return 0;
	default:
		*width = 4;
		*height = 3;
		return 1;
	}
}

// Game code reads this (under CTR_NATIVE) to widen the 3D projection and culling.
static inline int NativeSettings_WidescreenActive(void)
{
	int width;
	int height;
	return (g_settings.video.aspectMode != NATIVE_ASPECT_ORIGINAL) && NativeSettings_AspectRatio(&width, &height) && (width * 3 > height * 4);
}

// How much the 4:3 frame is compressed horizontally to show the wide view without
// distortion, (4/3) / aspect, in thousandths: 750 at 16:9, 1000 when not wide.
static inline int NativeSettings_WideSqueeze1000(void)
{
	int width;
	int height;
	if (!NativeSettings_WidescreenActive() || !NativeSettings_AspectRatio(&width, &height))
	{
		return 1000;
	}
	return (int)((4000LL * height + (3 * width) / 2) / (3LL * width));
}

static inline float NativeSettings_WideSqueeze(void)
{
	int width;
	int height;
	if (!NativeSettings_WidescreenActive() || !NativeSettings_AspectRatio(&width, &height))
	{
		return 1.0f;
	}
	return (4.0f * (float)height) / (3.0f * (float)width);
}

#ifdef __cplusplus
}
#endif

#endif
