#ifndef NATIVE_SETTINGS_H
#define NATIVE_SETTINGS_H

// NOTE: Shared with the C++ UI layer; keep to plain C types.

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
	// True 16:9: wider 3D view and culling, presented at 16:9 (see NativeSettings_WidescreenActive).
	NATIVE_ASPECT_WIDESCREEN,
	NATIVE_ASPECT_STRETCH,
	NATIVE_ASPECT_COUNT
};

#define NATIVE_RENDER_SCALE_MAX 8

// Frames per second shown. The game always runs its logic at 30; above that,
// in-between frames are interpolated (see native_interp.h).
#define NATIVE_FRAME_RATE_ORIGINAL 30
#define NATIVE_FRAME_RATE_MAX      360

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
	int hudPosition;
	// Internal resolution multiplier for the main render target (1 = original).
	int renderScale;
	int integerScaling;
	int scaleFilter;
	int textureFiltering;
	int frameRate;
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

struct NativeSettings
{
	struct NativeVideoSettings video;
	struct NativeInputSettings input;
};

extern struct NativeSettings g_settings;

void NativeSettings_SetDefaults(struct NativeSettings *settings);
void NativeSettings_SetInputDefaults(struct NativeInputSettings *input);
// Player 1 gets the classic keyboard layout; other players start with no keys.
void NativeSettings_SetPlayerInputDefaults(struct NativePlayerInputSettings *player, int playerIndex);
// Stable lower-case name of a pad button ("cross", "l2", "dpad_up"); used as a settings key.
const char *NativeSettings_PadButtonKey(int button);
// Loads <baseDir>/ctr-native.toml over the defaults. Returns 1 if a file was read.
int NativeSettings_Load(const char *baseDir);
// Writes the current settings back to the file chosen by NativeSettings_Load.
int NativeSettings_Save(void);
const char *NativeSettings_GetPath(void);

// Applies g_settings.video to the window and renderer (implemented by the platform layer).
void Platform_ApplyVideoSettings(void);
// Applies g_settings.input to the input layer.
void Platform_ApplyInputSettings(void);

// Game code reads this (under CTR_NATIVE) to widen the 3D projection and culling.
static inline int NativeSettings_WidescreenActive(void)
{
	return g_settings.video.aspectMode == NATIVE_ASPECT_WIDESCREEN;
}

#ifdef __cplusplus
}
#endif

#endif
