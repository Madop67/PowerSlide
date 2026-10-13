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

enum NativeAspectMode
{
	NATIVE_ASPECT_ORIGINAL = 0,
	NATIVE_ASPECT_STRETCH = 1,
	NATIVE_ASPECT_COUNT
};

#define NATIVE_RENDER_SCALE_MAX 8

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
	// Internal resolution multiplier for the main render target (1 = original).
	int renderScale;
	int integerScaling;
	int scaleFilter;
	int textureFiltering;
	int showFps;
};

struct NativeSettings
{
	struct NativeVideoSettings video;
};

extern struct NativeSettings g_settings;

void NativeSettings_SetDefaults(struct NativeSettings *settings);
// Loads <baseDir>/ctr-native.toml over the defaults. Returns 1 if a file was read.
int NativeSettings_Load(const char *baseDir);
// Writes the current settings back to the file chosen by NativeSettings_Load.
int NativeSettings_Save(void);
const char *NativeSettings_GetPath(void);

// Applies g_settings.video to the window and renderer (implemented by the platform layer).
void Platform_ApplyVideoSettings(void);

#ifdef __cplusplus
}
#endif

#endif
