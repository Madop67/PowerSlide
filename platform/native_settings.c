#include <platform/native_settings.h>

#include <macros.h>

#include "platform/native_log.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// NOTE: ctr-native.toml uses a small TOML subset: [section] headers and
// key = value lines holding integers, true/false, or "quoted names".
#define NATIVE_SETTINGS_FILE_NAME "ctr-native.toml"
#define NATIVE_SETTINGS_LINE_MAX  256

struct NativeSettings g_settings;

global_variable char s_settingsPath[1024];

enum NativeSettingType
{
	NATIVE_SETTING_INT,
	NATIVE_SETTING_BOOL,
	NATIVE_SETTING_ENUM,
};

struct NativeSettingDesc
{
	const char *section;
	const char *key;
	enum NativeSettingType type;
	size_t offset;
	int minValue;
	int maxValue;
	const char *const *names;
};

global_variable const char *const s_displayModeNames[NATIVE_DISPLAY_COUNT] = {"windowed", "fullscreen"};
global_variable const char *const s_aspectModeNames[NATIVE_ASPECT_COUNT] = {"original", "widescreen", "stretch"};
global_variable const char *const s_scaleFilterNames[NATIVE_SCALE_FILTER_COUNT] = {"sharp", "smooth"};

#define NATIVE_VIDEO_OFFSET(field) offsetof(struct NativeSettings, video.field)

global_variable const struct NativeSettingDesc s_settingDescs[] = {
	{"video", "display_mode", NATIVE_SETTING_ENUM, NATIVE_VIDEO_OFFSET(displayMode), 0, NATIVE_DISPLAY_COUNT - 1, s_displayModeNames},
	{"video", "window_width", NATIVE_SETTING_INT, NATIVE_VIDEO_OFFSET(windowWidth), 0, 16384, NULL},
	{"video", "window_height", NATIVE_SETTING_INT, NATIVE_VIDEO_OFFSET(windowHeight), 0, 16384, NULL},
	{"video", "aspect_ratio", NATIVE_SETTING_ENUM, NATIVE_VIDEO_OFFSET(aspectMode), 0, NATIVE_ASPECT_COUNT - 1, s_aspectModeNames},
	{"video", "render_scale", NATIVE_SETTING_INT, NATIVE_VIDEO_OFFSET(renderScale), 1, NATIVE_RENDER_SCALE_MAX, NULL},
	{"video", "integer_scaling", NATIVE_SETTING_BOOL, NATIVE_VIDEO_OFFSET(integerScaling), 0, 1, NULL},
	{"video", "scale_filter", NATIVE_SETTING_ENUM, NATIVE_VIDEO_OFFSET(scaleFilter), 0, NATIVE_SCALE_FILTER_COUNT - 1, s_scaleFilterNames},
	{"video", "texture_filtering", NATIVE_SETTING_BOOL, NATIVE_VIDEO_OFFSET(textureFiltering), 0, 1, NULL},
	{"video", "show_fps", NATIVE_SETTING_BOOL, NATIVE_VIDEO_OFFSET(showFps), 0, 1, NULL},
};

#define NATIVE_SETTING_COUNT ((int)(sizeof(s_settingDescs) / sizeof(s_settingDescs[0])))

internal int *NativeSettings_Field(struct NativeSettings *settings, const struct NativeSettingDesc *desc)
{
	return (int *)((char *)settings + desc->offset);
}

void NativeSettings_SetDefaults(struct NativeSettings *settings)
{
	memset(settings, 0, sizeof(*settings));
	settings->video.displayMode = NATIVE_DISPLAY_WINDOWED;
	settings->video.aspectMode = NATIVE_ASPECT_ORIGINAL;
	settings->video.renderScale = 1;
	settings->video.scaleFilter = NATIVE_SCALE_FILTER_SHARP;
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

internal int NativeSettings_ParseValue(const struct NativeSettingDesc *desc, const char *value, int *out)
{
	switch (desc->type)
	{
	case NATIVE_SETTING_BOOL:
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

	case NATIVE_SETTING_ENUM:
	{
		size_t length = strlen(value);
		if ((length < 2) || (value[0] != '"') || (value[length - 1] != '"'))
		{
			return 0;
		}
		for (int i = desc->minValue; i <= desc->maxValue; i++)
		{
			if ((strlen(desc->names[i]) == length - 2) && (strncmp(value + 1, desc->names[i], length - 2) == 0))
			{
				*out = i;
				return 1;
			}
		}
		return 0;
	}

	case NATIVE_SETTING_INT:
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
	}

	return 0;
}

internal const struct NativeSettingDesc *NativeSettings_Find(const char *section, const char *key)
{
	for (int i = 0; i < NATIVE_SETTING_COUNT; i++)
	{
		if ((strcmp(s_settingDescs[i].section, section) == 0) && (strcmp(s_settingDescs[i].key, key) == 0))
		{
			return &s_settingDescs[i];
		}
	}
	return NULL;
}

int NativeSettings_Load(const char *baseDir)
{
	NativeSettings_SetDefaults(&g_settings);

	if ((baseDir == NULL) || (baseDir[0] == '\0'))
	{
		baseDir = ".";
	}
	snprintf(s_settingsPath, sizeof(s_settingsPath), "%s/%s", baseDir, NATIVE_SETTINGS_FILE_NAME);

	FILE *file = fopen(s_settingsPath, "r");
	if (file == NULL)
	{
		Platform_Log("[CTR Native] Settings: %s not found, using defaults\n", s_settingsPath);
		return 0;
	}

	char line[NATIVE_SETTINGS_LINE_MAX];
	char section[64] = "";
	int lineNumber = 0;

	while (fgets(line, sizeof(line), file) != NULL)
	{
		lineNumber++;

		char *comment = strchr(line, '#');
		if (comment != NULL)
		{
			*comment = '\0';
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
		const struct NativeSettingDesc *desc = NativeSettings_Find(section, key);
		int parsed;

		if (desc == NULL)
		{
			Platform_LogWarn("[CTR Native] Settings: unknown key %s.%s ignored\n", section, key);
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
	Platform_Log("[CTR Native] Settings: loaded %s\n", s_settingsPath);
	return 1;
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
	for (int i = 0; i < NATIVE_SETTING_COUNT; i++)
	{
		const struct NativeSettingDesc *desc = &s_settingDescs[i];
		const int value = *NativeSettings_Field(&g_settings, desc);

		if ((section == NULL) || (strcmp(section, desc->section) != 0))
		{
			section = desc->section;
			fprintf(file, "\n[%s]\n", section);
		}

		switch (desc->type)
		{
		case NATIVE_SETTING_BOOL:
			fprintf(file, "%s = %s\n", desc->key, value ? "true" : "false");
			break;
		case NATIVE_SETTING_ENUM:
			fprintf(file, "%s = \"%s\"\n", desc->key, desc->names[value]);
			break;
		case NATIVE_SETTING_INT:
			fprintf(file, "%s = %d\n", desc->key, value);
			break;
		}
	}

	fclose(file);
	return 1;
}

const char *NativeSettings_GetPath(void)
{
	return s_settingsPath;
}
