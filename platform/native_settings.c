#include <platform/native_settings.h>

#include <macros.h>

#include "platform/native_log.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// NOTE: ctr-native.toml uses a small TOML subset: [section] headers and
// key = value lines holding integers, true/false, or "quoted names".
#define NATIVE_SETTINGS_FILE_NAME "ctr-native.toml"
#define NATIVE_PRESETS_FILE_NAME  "ctr-native-presets.toml"
#define NATIVE_SETTINGS_LINE_MAX  256
#define NATIVE_SETTINGS_DESC_MAX  320
#define NATIVE_SETTINGS_KEY_MAX   40
#define NATIVE_CVAR_TEXT_MAX      160

struct NativeSettings g_settings;

global_variable char s_settingsPath[1024];
global_variable char s_presetsPath[1024];

// The chosen preset settings while PS1 mode runs retail values in g_settings.
global_variable struct NativeSettings s_chosenSettings;
global_variable int s_ps1ModeApplied;

struct NativePreset
{
	char name[NATIVE_PRESET_NAME_MAX];
	struct NativeSettings settings;
};

global_variable struct NativePreset s_customPresets[NATIVE_PRESET_CUSTOM_MAX];
global_variable int s_customPresetCount;
global_variable const char *const s_builtinPresetNames[NATIVE_PRESET_BUILTIN_COUNT] = {"Vanilla", "Enhanced"};

global_variable const char *const s_displayModeNames[NATIVE_DISPLAY_COUNT] = {"windowed", "fullscreen"};
global_variable const char *const s_aspectModeNames[NATIVE_ASPECT_COUNT] = {"original", "widescreen", "ultrawide", "custom", "stretch"};
global_variable const char *const s_scaleFilterNames[NATIVE_SCALE_FILTER_COUNT] = {"sharp", "smooth"};
global_variable const char *const s_hudPositionNames[NATIVE_HUD_POSITION_COUNT] = {"edges", "centre"};
global_variable const char *const s_fastBootNames[NATIVE_FAST_BOOT_COUNT] = {"off", "title", "menu"};

// One section per player: [input.player1] .. [input.player4].
global_variable const char *const s_playerSections[NATIVE_INPUT_PLAYERS] = {"input.player1", "input.player2", "input.player3", "input.player4"};

global_variable const char *const s_padButtonKeys[NATIVE_PAD_BUTTON_COUNT] = {
	"cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2",
	"dpad_up", "dpad_down", "dpad_left", "dpad_right", "start", "select", "l3", "r3",
};

#define NATIVE_GLOBAL_OFFSET(field) offsetof(struct NativeSettings, global.field)
#define NATIVE_GAME_OFFSET(field)  offsetof(struct NativeSettings, game.field)
#define NATIVE_VIDEO_OFFSET(field) offsetof(struct NativeSettings, video.field)
#define NATIVE_PLAYER_OFFSET(player, field)                                                                                 \
	(offsetof(struct NativeSettings, input.players) + ((size_t)(player) * sizeof(struct NativePlayerInputSettings)) + \
	 offsetof(struct NativePlayerInputSettings, field))

#define P NATIVE_CVAR_FLAG_PRESET

global_variable const struct NativeCVar s_fixedDescs[] = {
	{"global", "ps1_mode", NATIVE_CVAR_BOOL, NATIVE_GLOBAL_OFFSET(ps1Mode), 0, 1, NULL, 0},
	{"game", "fast_boot", NATIVE_CVAR_ENUM, NATIVE_GAME_OFFSET(fastBoot), 0, NATIVE_FAST_BOOT_COUNT - 1, s_fastBootNames, P},
	{"game", "unlock_all_characters", NATIVE_CVAR_BOOL, NATIVE_GAME_OFFSET(unlockAllCharacters), 0, 1, NULL, P},
	{"game", "unlimited_turbo", NATIVE_CVAR_BOOL, NATIVE_GAME_OFFSET(unlimitedTurbo), 0, 1, NULL, P},
	{"game", "unlimited_wumpa", NATIVE_CVAR_BOOL, NATIVE_GAME_OFFSET(unlimitedWumpa), 0, 1, NULL, P},
	{"video", "display_mode", NATIVE_CVAR_ENUM, NATIVE_VIDEO_OFFSET(displayMode), 0, NATIVE_DISPLAY_COUNT - 1, s_displayModeNames, 0},
	{"video", "window_width", NATIVE_CVAR_INT, NATIVE_VIDEO_OFFSET(windowWidth), 0, 16384, NULL, 0},
	{"video", "window_height", NATIVE_CVAR_INT, NATIVE_VIDEO_OFFSET(windowHeight), 0, 16384, NULL, 0},
	{"video", "aspect_ratio", NATIVE_CVAR_ENUM, NATIVE_VIDEO_OFFSET(aspectMode), 0, NATIVE_ASPECT_COUNT - 1, s_aspectModeNames, P},
	{"video", "aspect_custom_width", NATIVE_CVAR_INT, NATIVE_VIDEO_OFFSET(aspectCustomWidth), 1, NATIVE_ASPECT_CUSTOM_MAX_TERM, NULL, P},
	{"video", "aspect_custom_height", NATIVE_CVAR_INT, NATIVE_VIDEO_OFFSET(aspectCustomHeight), 1, NATIVE_ASPECT_CUSTOM_MAX_TERM, NULL, P},
	{"video", "hud_position", NATIVE_CVAR_ENUM, NATIVE_VIDEO_OFFSET(hudPosition), 0, NATIVE_HUD_POSITION_COUNT - 1, s_hudPositionNames, P},
	{"video", "render_scale", NATIVE_CVAR_INT, NATIVE_VIDEO_OFFSET(renderScale), 1, NATIVE_RENDER_SCALE_MAX, NULL, P},
	{"video", "render_scale_custom", NATIVE_CVAR_BOOL, NATIVE_VIDEO_OFFSET(renderScaleCustom), 0, 1, NULL, P},
	{"video", "render_scale_percent", NATIVE_CVAR_INT, NATIVE_VIDEO_OFFSET(renderScalePercent), NATIVE_RENDER_SCALE_PERCENT_MIN, NATIVE_RENDER_SCALE_PERCENT_MAX, NULL, P},
	{"video", "integer_scaling", NATIVE_CVAR_BOOL, NATIVE_VIDEO_OFFSET(integerScaling), 0, 1, NULL, P},
	{"video", "scale_filter", NATIVE_CVAR_ENUM, NATIVE_VIDEO_OFFSET(scaleFilter), 0, NATIVE_SCALE_FILTER_COUNT - 1, s_scaleFilterNames, P},
	{"video", "texture_filtering", NATIVE_CVAR_BOOL, NATIVE_VIDEO_OFFSET(textureFiltering), 0, 1, NULL, P},
	{"video", "pause_full_color", NATIVE_CVAR_BOOL, NATIVE_VIDEO_OFFSET(pauseFullColor), 0, 1, NULL, P},
	{"video", "frame_rate", NATIVE_CVAR_INT, NATIVE_VIDEO_OFFSET(frameRate), NATIVE_FRAME_RATE_MIN, NATIVE_FRAME_RATE_MAX, NULL, P},
	{"video", "frame_rate_match_display", NATIVE_CVAR_BOOL, NATIVE_VIDEO_OFFSET(frameRateMatchDisplay), 0, 1, NULL, P},
	{"video", "show_fps", NATIVE_CVAR_BOOL, NATIVE_VIDEO_OFFSET(showFps), 0, 1, NULL, 0},
};

#undef P

// Fixed settings plus the per-player input settings generated by NativeSettings_RegisterDescs.
global_variable struct NativeCVar s_settingDescs[NATIVE_SETTINGS_DESC_MAX];
global_variable char s_generatedKeys[NATIVE_SETTINGS_DESC_MAX][NATIVE_SETTINGS_KEY_MAX];
global_variable int s_settingCount;

internal void NativeSettings_AddGenerated(int player, const char *key, enum NativeCVarType type, size_t offset, int minValue, int maxValue)
{
	if (s_settingCount >= NATIVE_SETTINGS_DESC_MAX)
	{
		return;
	}
	snprintf(s_generatedKeys[s_settingCount], NATIVE_SETTINGS_KEY_MAX, "%s", key);
	struct NativeCVar *desc = &s_settingDescs[s_settingCount++];
	desc->section = s_playerSections[player];
	desc->key = s_generatedKeys[s_settingCount - 1];
	desc->type = type;
	desc->offset = offset;
	desc->minValue = minValue;
	desc->maxValue = maxValue;
	desc->names = NULL;
	desc->flags = 0;
}

internal void NativeSettings_RegisterDescs(void)
{
	if (s_settingCount != 0)
	{
		return;
	}

	for (int i = 0; i < (int)(sizeof(s_fixedDescs) / sizeof(s_fixedDescs[0])); i++)
	{
		s_settingDescs[s_settingCount++] = s_fixedDescs[i];
	}

	char key[NATIVE_SETTINGS_KEY_MAX];
	for (int player = 0; player < NATIVE_INPUT_PLAYERS; player++)
	{
		NativeSettings_AddGenerated(player, "controller", NATIVE_CVAR_STRING, NATIVE_PLAYER_OFFSET(player, controllerId), 0, NATIVE_CONTROLLER_ID_MAX);
		NativeSettings_AddGenerated(player, "controller_name", NATIVE_CVAR_STRING, NATIVE_PLAYER_OFFSET(player, controllerName), 0, NATIVE_CONTROLLER_NAME_MAX);
		NativeSettings_AddGenerated(player, "stick_deadzone", NATIVE_CVAR_INT, NATIVE_PLAYER_OFFSET(player, stickDeadzone), 0, 50);
		NativeSettings_AddGenerated(player, "analog_press_point", NATIVE_CVAR_INT, NATIVE_PLAYER_OFFSET(player, analogPressPoint), 10, 90);
		NativeSettings_AddGenerated(player, "rumble", NATIVE_CVAR_BOOL, NATIVE_PLAYER_OFFSET(player, rumble), 0, 1);
		NativeSettings_AddGenerated(player, "rumble_strength", NATIVE_CVAR_INT, NATIVE_PLAYER_OFFSET(player, rumbleStrength), 0, 100);
		for (int button = 0; button < NATIVE_PAD_BUTTON_COUNT; button++)
		{
			snprintf(key, sizeof(key), "keyboard_%s", s_padButtonKeys[button]);
			NativeSettings_AddGenerated(player, key, NATIVE_CVAR_SCANCODE, NATIVE_PLAYER_OFFSET(player, keyboard[button][0]), 0, 0);
			snprintf(key, sizeof(key), "keyboard_%s_alt", s_padButtonKeys[button]);
			NativeSettings_AddGenerated(player, key, NATIVE_CVAR_SCANCODE, NATIVE_PLAYER_OFFSET(player, keyboard[button][1]), 0, 0);
			snprintf(key, sizeof(key), "gamepad_%s", s_padButtonKeys[button]);
			NativeSettings_AddGenerated(player, key, NATIVE_CVAR_PAD_BINDING, NATIVE_PLAYER_OFFSET(player, gamepad[button]), 0, 0);
		}
	}
}

const char *NativeSettings_PadButtonKey(int button)
{
	return ((button >= 0) && (button < NATIVE_PAD_BUTTON_COUNT)) ? s_padButtonKeys[button] : "";
}

internal int *NativeSettings_Field(struct NativeSettings *settings, const struct NativeCVar *desc)
{
	return (int *)((char *)settings + desc->offset);
}

internal char *NativeSettings_TextField(struct NativeSettings *settings, const struct NativeCVar *desc)
{
	return (char *)settings + desc->offset;
}

// Matches the keyboard and controller layout native builds always shipped with.
void NativeSettings_SetPlayerInputDefaults(struct NativePlayerInputSettings *input, int playerIndex)
{
	memset(input, 0, sizeof(*input));

	input->gamepad[NATIVE_PAD_CROSS] = SDL_GAMEPAD_BUTTON_SOUTH;
	input->gamepad[NATIVE_PAD_CIRCLE] = SDL_GAMEPAD_BUTTON_EAST;
	input->gamepad[NATIVE_PAD_SQUARE] = SDL_GAMEPAD_BUTTON_WEST;
	input->gamepad[NATIVE_PAD_TRIANGLE] = SDL_GAMEPAD_BUTTON_NORTH;
	input->gamepad[NATIVE_PAD_L1] = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
	input->gamepad[NATIVE_PAD_R1] = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
	input->gamepad[NATIVE_PAD_L2] = NATIVE_PAD_BIND_AXIS | SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
	input->gamepad[NATIVE_PAD_R2] = NATIVE_PAD_BIND_AXIS | SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
	input->gamepad[NATIVE_PAD_UP] = SDL_GAMEPAD_BUTTON_DPAD_UP;
	input->gamepad[NATIVE_PAD_DOWN] = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
	input->gamepad[NATIVE_PAD_LEFT] = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
	input->gamepad[NATIVE_PAD_RIGHT] = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
	input->gamepad[NATIVE_PAD_START] = SDL_GAMEPAD_BUTTON_START;
	input->gamepad[NATIVE_PAD_SELECT] = SDL_GAMEPAD_BUTTON_BACK;
	input->gamepad[NATIVE_PAD_L3] = SDL_GAMEPAD_BUTTON_LEFT_STICK;
	input->gamepad[NATIVE_PAD_R3] = SDL_GAMEPAD_BUTTON_RIGHT_STICK;

	input->stickDeadzone = 0;
	input->analogPressPoint = 50;
	input->rumble = 1;
	input->rumbleStrength = 100;

	if (playerIndex != 0)
	{
		return;
	}

	input->keyboard[NATIVE_PAD_CROSS][0] = SDL_SCANCODE_C;
	input->keyboard[NATIVE_PAD_CIRCLE][0] = SDL_SCANCODE_V;
	input->keyboard[NATIVE_PAD_SQUARE][0] = SDL_SCANCODE_X;
	input->keyboard[NATIVE_PAD_TRIANGLE][0] = SDL_SCANCODE_Z;
	input->keyboard[NATIVE_PAD_L1][0] = SDL_SCANCODE_LSHIFT;
	input->keyboard[NATIVE_PAD_R1][0] = SDL_SCANCODE_RSHIFT;
	input->keyboard[NATIVE_PAD_L2][0] = SDL_SCANCODE_LCTRL;
	input->keyboard[NATIVE_PAD_R2][0] = SDL_SCANCODE_RCTRL;
	input->keyboard[NATIVE_PAD_UP][0] = SDL_SCANCODE_UP;
	input->keyboard[NATIVE_PAD_DOWN][0] = SDL_SCANCODE_DOWN;
	input->keyboard[NATIVE_PAD_LEFT][0] = SDL_SCANCODE_LEFT;
	input->keyboard[NATIVE_PAD_RIGHT][0] = SDL_SCANCODE_RIGHT;
	input->keyboard[NATIVE_PAD_START][0] = SDL_SCANCODE_RETURN;
	input->keyboard[NATIVE_PAD_SELECT][0] = SDL_SCANCODE_SPACE;
	input->keyboard[NATIVE_PAD_L3][0] = SDL_SCANCODE_LEFTBRACKET;
	input->keyboard[NATIVE_PAD_R3][0] = SDL_SCANCODE_RIGHTBRACKET;
}

void NativeSettings_SetInputDefaults(struct NativeInputSettings *input)
{
	for (int player = 0; player < NATIVE_INPUT_PLAYERS; player++)
	{
		NativeSettings_SetPlayerInputDefaults(&input->players[player], player);
	}
}

void NativeSettings_SetDefaults(struct NativeSettings *settings)
{
	memset(settings, 0, sizeof(*settings));
	settings->video.displayMode = NATIVE_DISPLAY_WINDOWED;
	settings->video.aspectMode = NATIVE_ASPECT_ORIGINAL;
	settings->video.aspectCustomWidth = 16;
	settings->video.aspectCustomHeight = 10;
	settings->video.renderScale = 1;
	settings->video.renderScalePercent = 150;
	settings->video.frameRate = NATIVE_FRAME_RATE_ORIGINAL;
	settings->video.scaleFilter = NATIVE_SCALE_FILTER_SHARP;
	settings->video.pauseFullColor = 1;
	NativeSettings_SetInputDefaults(&settings->input);
}

// Copies the preset settings from one settings block to another.
internal void NativeSettings_CopyPresetFields(struct NativeSettings *to, const struct NativeSettings *from)
{
	NativeSettings_RegisterDescs();
	for (int i = 0; i < s_settingCount; i++)
	{
		const struct NativeCVar *desc = &s_settingDescs[i];
		if ((desc->flags & NATIVE_CVAR_FLAG_PRESET) != 0)
		{
			// Preset settings are all ints.
			*(int *)((char *)to + desc->offset) = *(const int *)((const char *)from + desc->offset);
		}
	}
}

void NativeSettings_SetRetail(struct NativeSettings *settings)
{
	struct NativeSettings retail;
	NativeSettings_SetDefaults(&retail);
	// The default keeps the full-colour pause screen; retail shows the sepia screenshot.
	retail.video.pauseFullColor = 0;
	NativeSettings_CopyPresetFields(settings, &retail);
}

void NativeSettings_RefreshPs1Mode(void)
{
	if (g_settings.global.ps1Mode && !s_ps1ModeApplied)
	{
		NativeSettings_CopyPresetFields(&s_chosenSettings, &g_settings);
		NativeSettings_SetRetail(&g_settings);
		s_ps1ModeApplied = 1;
	}
	else if (!g_settings.global.ps1Mode && s_ps1ModeApplied)
	{
		NativeSettings_CopyPresetFields(&g_settings, &s_chosenSettings);
		s_ps1ModeApplied = 0;
	}
}

internal char *NativeSettings_Trim(char *text)
{
	while (isspace((unsigned char)*text))
	{
		text++;
	}

	char *end = text + strlen(text);
	while ((end > text) && isspace((unsigned char)end[-1]))
	{
		end--;
	}
	*end = '\0';
	return text;
}

// Copies a "quoted" value without its quotes; returns 0 if it isn't quoted.
internal int NativeSettings_Unquote(const char *value, char *out, size_t outSize)
{
	size_t length = strlen(value);
	if ((length < 2) || (value[0] != '"') || (value[length - 1] != '"') || (length - 2 >= outSize))
	{
		return 0;
	}
	memcpy(out, value + 1, length - 2);
	out[length - 2] = '\0';
	return 1;
}

internal int NativeSettings_ParsePadBinding(const char *name, int *out)
{
	if (strcmp(name, "none") == 0)
	{
		*out = NATIVE_PAD_BIND_NONE;
		return 1;
	}
	if ((name[0] == '+') || (name[0] == '-'))
	{
		SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(name + 1);
		if (axis == SDL_GAMEPAD_AXIS_INVALID)
		{
			return 0;
		}
		*out = NATIVE_PAD_BIND_AXIS | (int)axis | ((name[0] == '-') ? NATIVE_PAD_BIND_NEGATIVE : 0);
		return 1;
	}
	SDL_GamepadButton button = SDL_GetGamepadButtonFromString(name);
	if (button == SDL_GAMEPAD_BUTTON_INVALID)
	{
		return 0;
	}
	*out = (int)button;
	return 1;
}

internal void NativeSettings_FormatPadBinding(int binding, char *out, size_t outSize)
{
	const char *name = NULL;
	if (binding == NATIVE_PAD_BIND_NONE)
	{
		snprintf(out, outSize, "none");
		return;
	}
	if ((binding & NATIVE_PAD_BIND_AXIS) != 0)
	{
		name = SDL_GetGamepadStringForAxis((SDL_GamepadAxis)(binding & ~(NATIVE_PAD_BIND_AXIS | NATIVE_PAD_BIND_NEGATIVE)));
		snprintf(out, outSize, "%c%s", ((binding & NATIVE_PAD_BIND_NEGATIVE) != 0) ? '-' : '+', name ? name : "?");
		return;
	}
	name = SDL_GetGamepadStringForButton((SDL_GamepadButton)binding);
	snprintf(out, outSize, "%s", name ? name : "none");
}

internal int NativeSettings_ParseValue(const struct NativeCVar *desc, const char *value, int *out)
{
	char text[64];

	switch (desc->type)
	{
	case NATIVE_CVAR_BOOL:
		if (strcmp(value, "true") == 0)
		{
			*out = 1;
			return 1;
		}
		if (strcmp(value, "false") == 0)
		{
			*out = 0;
			return 1;
		}
		return 0;

	case NATIVE_CVAR_ENUM:
		if (!NativeSettings_Unquote(value, text, sizeof(text)))
		{
			return 0;
		}
		for (int i = desc->minValue; i <= desc->maxValue; i++)
		{
			if (strcmp(text, desc->names[i]) == 0)
			{
				*out = i;
				return 1;
			}
		}
		return 0;

	case NATIVE_CVAR_INT:
	{
		char *end = NULL;
		long parsed = strtol(value, &end, 10);
		if ((end == value) || (*end != '\0') || (parsed < desc->minValue) || (parsed > desc->maxValue))
		{
			return 0;
		}
		*out = (int)parsed;
		return 1;
	}

	case NATIVE_CVAR_SCANCODE:
		if (!NativeSettings_Unquote(value, text, sizeof(text)))
		{
			return 0;
		}
		if (strcmp(text, "none") == 0)
		{
			*out = SDL_SCANCODE_UNKNOWN;
			return 1;
		}
		*out = (int)SDL_GetScancodeFromName(text);
		return *out != SDL_SCANCODE_UNKNOWN;

	case NATIVE_CVAR_PAD_BINDING:
		return NativeSettings_Unquote(value, text, sizeof(text)) && NativeSettings_ParsePadBinding(text, out);

	case NATIVE_CVAR_STRING:
		// Read through NativeSettings_ParseText.
		return 0;
	}

	return 0;
}

internal const struct NativeCVar *NativeSettings_Find(const char *section, const char *key)
{
	for (int i = 0; i < s_settingCount; i++)
	{
		if ((strcmp(s_settingDescs[i].section, section) == 0) && (strcmp(s_settingDescs[i].key, key) == 0))
		{
			return &s_settingDescs[i];
		}
	}
	return NULL;
}

int NativeCVar_Count(void)
{
	NativeSettings_RegisterDescs();
	return s_settingCount;
}

const struct NativeCVar *NativeCVar_At(int index)
{
	NativeSettings_RegisterDescs();
	return ((index >= 0) && (index < s_settingCount)) ? &s_settingDescs[index] : NULL;
}

const struct NativeCVar *NativeCVar_Find(const char *name)
{
	char section[64];
	const char *dot = (name != NULL) ? strrchr(name, '.') : NULL;

	// Sections hold dots ("input.player1"); keys never do.
	if ((dot == NULL) || ((size_t)(dot - name) >= sizeof(section)))
	{
		return NULL;
	}
	memcpy(section, name, (size_t)(dot - name));
	section[dot - name] = '\0';

	NativeSettings_RegisterDescs();
	return NativeSettings_Find(section, dot + 1);
}

internal int NativeSettings_SetFromText(struct NativeSettings *settings, const struct NativeCVar *cvar, const char *text)
{
	char quoted[NATIVE_CVAR_TEXT_MAX];
	int value;

	if ((cvar == NULL) || (text == NULL))
	{
		return 0;
	}

	// Names may come unquoted from the command line.
	const int isName = (cvar->type == NATIVE_CVAR_ENUM) || (cvar->type == NATIVE_CVAR_SCANCODE) || (cvar->type == NATIVE_CVAR_PAD_BINDING) ||
	                   (cvar->type == NATIVE_CVAR_STRING);
	if (isName && (text[0] != '"'))
	{
		snprintf(quoted, sizeof(quoted), "\"%s\"", text);
		text = quoted;
	}

	if (cvar->type == NATIVE_CVAR_STRING)
	{
		char unquoted[NATIVE_CVAR_TEXT_MAX];
		if (!NativeSettings_Unquote(text, unquoted, (size_t)cvar->maxValue))
		{
			return 0;
		}
		memcpy(NativeSettings_TextField(settings, cvar), unquoted, strlen(unquoted) + 1);
		return 1;
	}

	if (!NativeSettings_ParseValue(cvar, text, &value))
	{
		return 0;
	}
	*NativeSettings_Field(settings, cvar) = value;
	return 1;
}

int NativeCVar_SetFromText(const struct NativeCVar *cvar, const char *text)
{
	return NativeSettings_SetFromText(&g_settings, cvar, text);
}

internal void NativeSettings_Format(const struct NativeSettings *settings, const struct NativeCVar *cvar, char *out, size_t outSize)
{
	char text[64];

	if (cvar->type == NATIVE_CVAR_STRING)
	{
		snprintf(out, outSize, "\"%s\"", NativeSettings_TextField((struct NativeSettings *)settings, cvar));
		return;
	}

	const int value = *NativeSettings_Field((struct NativeSettings *)settings, cvar);
	switch (cvar->type)
	{
	case NATIVE_CVAR_BOOL:
		snprintf(out, outSize, "%s", value ? "true" : "false");
		break;
	case NATIVE_CVAR_ENUM:
		snprintf(out, outSize, "\"%s\"", cvar->names[value]);
		break;
	case NATIVE_CVAR_SCANCODE:
	{
		const char *name = (value != SDL_SCANCODE_UNKNOWN) ? SDL_GetScancodeName((SDL_Scancode)value) : "";
		snprintf(out, outSize, "\"%s\"", (name[0] != '\0') ? name : "none");
		break;
	}
	case NATIVE_CVAR_PAD_BINDING:
		NativeSettings_FormatPadBinding(value, text, sizeof(text));
		snprintf(out, outSize, "\"%s\"", text);
		break;
	default:
		snprintf(out, outSize, "%d", value);
		break;
	}
}

void NativeCVar_Format(const struct NativeCVar *cvar, char *out, size_t outSize)
{
	NativeSettings_Format(&g_settings, cvar, out, outSize);
}

int NativeSettings_ApplyOverrides(int argc, char **argv)
{
	for (int i = 1; i < argc; i++)
	{
		char name[NATIVE_CVAR_TEXT_MAX];
		const char *argument;
		const char *equals;
		const struct NativeCVar *cvar;

		if (strcmp(argv[i], "--set") != 0)
		{
			continue;
		}
		if (i + 1 >= argc)
		{
			Platform_LogWarn("[CTR Native] Settings: --set needs <name>=<value>\n");
			return 0;
		}

		argument = argv[++i];
		equals = strchr(argument, '=');
		if ((equals == NULL) || ((size_t)(equals - argument) >= sizeof(name)))
		{
			Platform_LogWarn("[CTR Native] Settings: --set %s: expected <name>=<value>\n", argument);
			return 0;
		}
		memcpy(name, argument, (size_t)(equals - argument));
		name[equals - argument] = '\0';

		cvar = NativeCVar_Find(name);
		if (cvar == NULL)
		{
			Platform_LogWarn("[CTR Native] Settings: --set: unknown setting %s\n", name);
			return 0;
		}
		if (!NativeCVar_SetFromText(cvar, equals + 1))
		{
			Platform_LogWarn("[CTR Native] Settings: --set: invalid value for %s: %s\n", name, equals + 1);
			return 0;
		}
		Platform_Log("[CTR Native] Settings: %s = %s (from --set)\n", name, equals + 1);
	}
	NativeSettings_RefreshPs1Mode();
	return 1;
}

// The old shared layout was loaded into player 1: every player gets its controller layout
// and analog settings, and the keyboard keys move to the player the keyboard drove.
internal void NativeSettings_MigrateLegacyInput(int keyboardPlayer)
{
	struct NativePlayerInputSettings *players = g_settings.input.players;

	for (int player = 1; player < NATIVE_INPUT_PLAYERS; player++)
	{
		memcpy(players[player].gamepad, players[0].gamepad, sizeof(players[0].gamepad));
		players[player].stickDeadzone = players[0].stickDeadzone;
		players[player].analogPressPoint = players[0].analogPressPoint;
		players[player].rumble = players[0].rumble;
		players[player].rumbleStrength = players[0].rumbleStrength;
	}

	if (keyboardPlayer != 0)
	{
		memcpy(players[keyboardPlayer].keyboard, players[0].keyboard, sizeof(players[0].keyboard));
		memset(players[0].keyboard, 0, sizeof(players[0].keyboard));
	}

	Platform_Log("[CTR Native] Settings: converted the shared [input] layout to per-player settings\n");
}

internal void NativePreset_LoadFile(void);

int NativeSettings_Load(const char *baseDir)
{
	NativeSettings_RegisterDescs();
	NativeSettings_SetDefaults(&g_settings);
	s_ps1ModeApplied = 0;

	if ((baseDir == NULL) || (baseDir[0] == '\0'))
	{
		baseDir = ".";
	}
	snprintf(s_settingsPath, sizeof(s_settingsPath), "%s/%s", baseDir, NATIVE_SETTINGS_FILE_NAME);
	snprintf(s_presetsPath, sizeof(s_presetsPath), "%s/%s", baseDir, NATIVE_PRESETS_FILE_NAME);
	NativePreset_LoadFile();

	FILE *file = fopen(s_settingsPath, "r");
	if (file == NULL)
	{
		Platform_Log("[CTR Native] Settings: %s not found, using defaults\n", s_settingsPath);
		return 0;
	}

	char line[NATIVE_SETTINGS_LINE_MAX];
	char section[64] = "";
	int lineNumber = 0;
	int legacyInput = 0;
	int legacyKeyboardPlayer = 0;

	while (fgets(line, sizeof(line), file) != NULL)
	{
		lineNumber++;

		// Comments start at '#' outside quotes (key names like "#" are quoted).
		int inQuotes = 0;
		for (char *c = line; *c != '\0'; c++)
		{
			if (*c == '"')
			{
				inQuotes = !inQuotes;
			}
			else if ((*c == '#') && !inQuotes)
			{
				*c = '\0';
				break;
			}
		}

		char *text = NativeSettings_Trim(line);
		if (text[0] == '\0')
		{
			continue;
		}

		if (text[0] == '[')
		{
			char *close = strchr(text, ']');
			if (close != NULL)
			{
				*close = '\0';
				snprintf(section, sizeof(section), "%s", NativeSettings_Trim(text + 1));
			}
			continue;
		}

		char *equals = strchr(text, '=');
		if (equals == NULL)
		{
			Platform_LogWarn("[CTR Native] Settings: line %d ignored (expected key = value)\n", lineNumber);
			continue;
		}
		*equals = '\0';

		const char *key = NativeSettings_Trim(text);
		const char *value = NativeSettings_Trim(equals + 1);
		const char *lookupSection = section;
		int parsed;

		// Before per-player input, [input] held one shared layout; read it as player 1's.
		if (strcmp(section, "input") == 0)
		{
			legacyInput = 1;
			if (strcmp(key, "keyboard_player") == 0)
			{
				char *end = NULL;
				const long player = strtol(value, &end, 10);
				if ((end != value) && (*end == '\0') && (player >= 1) && (player <= NATIVE_INPUT_PLAYERS))
				{
					legacyKeyboardPlayer = (int)player - 1;
				}
				continue;
			}
			lookupSection = s_playerSections[0];
		}

		const struct NativeCVar *desc = NativeSettings_Find(lookupSection, key);
		if (desc == NULL)
		{
			Platform_LogWarn("[CTR Native] Settings: unknown key %s.%s ignored\n", section, key);
			continue;
		}
		if (desc->type == NATIVE_CVAR_STRING)
		{
			if (!NativeSettings_Unquote(value, NativeSettings_TextField(&g_settings, desc), (size_t)desc->maxValue))
			{
				Platform_LogWarn("[CTR Native] Settings: invalid value for %s.%s ignored\n", section, key);
			}
			continue;
		}
		if (!NativeSettings_ParseValue(desc, value, &parsed))
		{
			Platform_LogWarn("[CTR Native] Settings: invalid value for %s.%s ignored\n", section, key);
			continue;
		}

		*NativeSettings_Field(&g_settings, desc) = parsed;
	}

	fclose(file);

	if (legacyInput)
	{
		NativeSettings_MigrateLegacyInput(legacyKeyboardPlayer);
	}
	NativeSettings_RefreshPs1Mode();
	Platform_Log("[CTR Native] Settings: loaded %s\n", s_settingsPath);
	return 1;
}

void NativeSettings_LoadVanilla(void)
{
	NativeSettings_RegisterDescs();
	NativeSettings_SetDefaults(&g_settings);
	s_ps1ModeApplied = 0;
	s_settingsPath[0] = '\0';
	s_presetsPath[0] = '\0';
	s_customPresetCount = 0;
	Platform_Log("[CTR Native] Settings: --vanilla, using defaults; changes are not saved\n");
}

int NativeSettings_Save(void)
{
	if (s_settingsPath[0] == '\0')
	{
		return 0;
	}

	FILE *file = fopen(s_settingsPath, "w");
	if (file == NULL)
	{
		Platform_LogWarn("[CTR Native] Settings: cannot write %s\n", s_settingsPath);
		return 0;
	}

	fprintf(file, "# CTR Native settings. Edited by the in-game menu (F1); unknown keys are ignored.\n");

	const char *section = NULL;
	char text[NATIVE_CVAR_TEXT_MAX];
	for (int i = 0; i < s_settingCount; i++)
	{
		const struct NativeCVar *desc = &s_settingDescs[i];

		if ((section == NULL) || (strcmp(section, desc->section) != 0))
		{
			section = desc->section;
			fprintf(file, "\n[%s]\n", section);
		}

		// PS1 mode only runs retail values; the file keeps the chosen ones.
		const int chosen = s_ps1ModeApplied && ((desc->flags & NATIVE_CVAR_FLAG_PRESET) != 0);
		NativeSettings_Format(chosen ? &s_chosenSettings : &g_settings, desc, text, sizeof(text));
		fprintf(file, "%s = %s\n", desc->key, text);
	}

	fclose(file);
	return 1;
}

const char *NativeSettings_GetPath(void)
{
	return s_settingsPath;
}

//------------------------------------------------------------------------------------------------------------------------
// Presets

// Built-in presets are the retail values, plus Enhanced's changes.
internal void NativePreset_BuiltinSettings(int index, struct NativeSettings *settings)
{
	NativeSettings_SetDefaults(settings);
	NativeSettings_SetRetail(settings);
	if (index != NATIVE_PRESET_ENHANCED)
	{
		return;
	}

	settings->game.fastBoot = NATIVE_FAST_BOOT_TITLE;
	settings->video.aspectMode = NATIVE_ASPECT_WIDESCREEN;
	settings->video.hudPosition = NATIVE_HUD_POSITION_EDGES;
	settings->video.renderScale = 4;
	settings->video.scaleFilter = NATIVE_SCALE_FILTER_SMOOTH;
	settings->video.pauseFullColor = 1;
	settings->video.frameRate = 60;
	settings->video.frameRateMatchDisplay = 1;
}

internal int NativePreset_PresetFieldsEqual(const struct NativeSettings *a, const struct NativeSettings *b)
{
	NativeSettings_RegisterDescs();
	for (int i = 0; i < s_settingCount; i++)
	{
		const struct NativeCVar *desc = &s_settingDescs[i];
		if (((desc->flags & NATIVE_CVAR_FLAG_PRESET) != 0) &&
		    (*(const int *)((const char *)a + desc->offset) != *(const int *)((const char *)b + desc->offset)))
		{
			return 0;
		}
	}
	return 1;
}

// Preset names compare without case (ASCII; isn't an SDL call, so the wasm guest needs no import).
internal int NativePreset_NameCompare(const char *a, const char *b)
{
	while ((*a != '\0') && (tolower((unsigned char)*a) == tolower((unsigned char)*b)))
	{
		a++;
		b++;
	}
	return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int NativePreset_Count(void)
{
	return NATIVE_PRESET_BUILTIN_COUNT + s_customPresetCount;
}

const char *NativePreset_Name(int index)
{
	if ((index >= 0) && (index < NATIVE_PRESET_BUILTIN_COUNT))
	{
		return s_builtinPresetNames[index];
	}
	index -= NATIVE_PRESET_BUILTIN_COUNT;
	return ((index >= 0) && (index < s_customPresetCount)) ? s_customPresets[index].name : "";
}

int NativePreset_IsCustom(int index)
{
	return (index >= NATIVE_PRESET_BUILTIN_COUNT) && (index < NativePreset_Count());
}

int NativePreset_Find(const char *name)
{
	for (int i = 0; i < NativePreset_Count(); i++)
	{
		if (NativePreset_NameCompare(NativePreset_Name(i), name) == 0)
		{
			return i;
		}
	}
	return -1;
}

internal void NativePreset_Settings(int index, struct NativeSettings *settings)
{
	if (NativePreset_IsCustom(index))
	{
		*settings = s_customPresets[index - NATIVE_PRESET_BUILTIN_COUNT].settings;
		return;
	}
	NativePreset_BuiltinSettings(index, settings);
}

int NativePreset_Matches(int index)
{
	struct NativeSettings preset;
	if ((index < 0) || (index >= NativePreset_Count()))
	{
		return 0;
	}
	NativePreset_Settings(index, &preset);
	return NativePreset_PresetFieldsEqual(&g_settings, &preset);
}

void NativePreset_Apply(int index)
{
	struct NativeSettings preset;
	if ((index < 0) || (index >= NativePreset_Count()))
	{
		return;
	}
	NativePreset_Settings(index, &preset);
	NativeSettings_CopyPresetFields(&g_settings, &preset);
}

// Custom presets are saved as [preset.N] sections: a quoted name, then
// "<section>.<key> = value" lines for every preset setting.
internal void NativePreset_SaveFile(void)
{
	if (s_presetsPath[0] == '\0')
	{
		return;
	}

	FILE *file = fopen(s_presetsPath, "w");
	if (file == NULL)
	{
		Platform_LogWarn("[CTR Native] Settings: cannot write %s\n", s_presetsPath);
		return;
	}

	fprintf(file, "# CTR Native custom presets. Edited by the in-game menu (F1).\n");

	char text[NATIVE_CVAR_TEXT_MAX];
	for (int p = 0; p < s_customPresetCount; p++)
	{
		const struct NativePreset *preset = &s_customPresets[p];
		fprintf(file, "\n[preset.%d]\nname = \"%s\"\n", p + 1, preset->name);
		for (int i = 0; i < s_settingCount; i++)
		{
			const struct NativeCVar *desc = &s_settingDescs[i];
			if ((desc->flags & NATIVE_CVAR_FLAG_PRESET) != 0)
			{
				NativeSettings_Format(&preset->settings, desc, text, sizeof(text));
				fprintf(file, "%s.%s = %s\n", desc->section, desc->key, text);
			}
		}
	}

	fclose(file);
}

internal void NativePreset_LoadFile(void)
{
	s_customPresetCount = 0;

	FILE *file = fopen(s_presetsPath, "r");
	if (file == NULL)
	{
		return;
	}

	char line[NATIVE_SETTINGS_LINE_MAX];
	struct NativePreset *preset = NULL;
	while (fgets(line, sizeof(line), file) != NULL)
	{
		char *text = NativeSettings_Trim(line);
		if ((text[0] == '\0') || (text[0] == '#'))
		{
			continue;
		}

		// Each [preset.N] starts a preset, holding the retail values until its lines say otherwise.
		if (text[0] == '[')
		{
			preset = NULL;
			if ((strncmp(text, "[preset.", 8) == 0) && (s_customPresetCount < NATIVE_PRESET_CUSTOM_MAX))
			{
				preset = &s_customPresets[s_customPresetCount++];
				preset->name[0] = '\0';
				NativeSettings_SetDefaults(&preset->settings);
				NativeSettings_SetRetail(&preset->settings);
			}
			continue;
		}

		char *equals = strchr(text, '=');
		if ((preset == NULL) || (equals == NULL))
		{
			continue;
		}
		*equals = '\0';
		const char *key = NativeSettings_Trim(text);
		const char *value = NativeSettings_Trim(equals + 1);

		if (strcmp(key, "name") == 0)
		{
			NativeSettings_Unquote(value, preset->name, sizeof(preset->name));
			continue;
		}

		const struct NativeCVar *desc = NativeCVar_Find(key);
		if ((desc == NULL) || ((desc->flags & NATIVE_CVAR_FLAG_PRESET) == 0) || !NativeSettings_SetFromText(&preset->settings, desc, value))
		{
			Platform_LogWarn("[CTR Native] Settings: preset key %s ignored\n", key);
		}
	}
	fclose(file);

	// Drop presets without a usable name.
	int kept = 0;
	for (int p = 0; p < s_customPresetCount; p++)
	{
		const char *name = s_customPresets[p].name;
		int duplicate = 0;
		for (int i = 0; i < kept; i++)
		{
			duplicate |= NativePreset_NameCompare(s_customPresets[i].name, name) == 0;
		}
		for (int i = 0; i < NATIVE_PRESET_BUILTIN_COUNT; i++)
		{
			duplicate |= NativePreset_NameCompare(s_builtinPresetNames[i], name) == 0;
		}
		if ((name[0] != '\0') && !duplicate)
		{
			s_customPresets[kept++] = s_customPresets[p];
		}
	}
	s_customPresetCount = kept;
	Platform_Log("[CTR Native] Settings: loaded %d custom preset(s) from %s\n", s_customPresetCount, s_presetsPath);
}

int NativePreset_SaveCustom(const char *name)
{
	char clean[NATIVE_PRESET_NAME_MAX];
	int length = 0;

	// Quotes and control characters can't be stored in the file's quoted names.
	for (const char *c = name; (*c != '\0') && (length < NATIVE_PRESET_NAME_MAX - 1); c++)
	{
		if ((*c != '"') && (*c != '\\') && ((unsigned char)*c >= 0x20))
		{
			clean[length++] = *c;
		}
	}
	clean[length] = '\0';
	char *trimmed = NativeSettings_Trim(clean);

	int index = NativePreset_Find(trimmed);
	if ((trimmed[0] == '\0') || ((index >= 0) && !NativePreset_IsCustom(index)))
	{
		return -1;
	}
	if (index < 0)
	{
		if (s_customPresetCount >= NATIVE_PRESET_CUSTOM_MAX)
		{
			return -1;
		}
		index = NATIVE_PRESET_BUILTIN_COUNT + s_customPresetCount++;
	}

	struct NativePreset *preset = &s_customPresets[index - NATIVE_PRESET_BUILTIN_COUNT];
	snprintf(preset->name, sizeof(preset->name), "%s", trimmed);
	NativeSettings_SetDefaults(&preset->settings);
	NativeSettings_CopyPresetFields(&preset->settings, &g_settings);
	NativePreset_SaveFile();
	return index;
}

void NativePreset_DeleteCustom(int index)
{
	if (!NativePreset_IsCustom(index))
	{
		return;
	}
	for (int p = index - NATIVE_PRESET_BUILTIN_COUNT; p < s_customPresetCount - 1; p++)
	{
		s_customPresets[p] = s_customPresets[p + 1];
	}
	s_customPresetCount--;
	NativePreset_SaveFile();
}
