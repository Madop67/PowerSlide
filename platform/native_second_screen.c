#include <common.h>
#include <macros.h>

#include <platform/native_hooks.h>
#include <platform/native_second_screen.h>
#include <platform/native_settings.h>

#include "platform/native_glad.h"
#include "platform/native_log.h"
#include "platform/native_product.h"
#include "platform/native_renderer.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Dual Screen (second_screen.*): see native_second_screen.h.
//
// Each tick, BeforeDraw copies what the pages show into g_secondScreenState. It only
// reads game memory, so recordings and replays play the same with it on. The level's
// minimap is decoded from VRAM once per level; the UI uploads it as a texture.
//
// "Display" mode opens a borderless fullscreen window on another display, with its
// own GL context sharing objects (textures, buffers, shaders) with the main one, so
// the main renderer's GL state is never touched. It is drawn after the main window's
// swap. "Inset" mode draws the same pages inside the main window (native_ui.cpp).

extern SDL_Window *g_window;

struct NativeSecondScreenState g_secondScreenState;

// Map image placement in HUD coordinates: retail draws the minimap with its
// bottom-right corner here in 1P (UI_RenderFrame_Racing).
#define NATIVE_MAP_HUD_RIGHT  500
#define NATIVE_MAP_HUD_BOTTOM 195
// UI_Map_GetIconPos's vertical origin offset and the map dot icon.
#define NATIVE_MAP_ICON_Y_OFFSET 0x10
#define NATIVE_MAP_DOT_ICON      0x31
#define NATIVE_MAP_ICON_GROUP    5

// The second window redraws at most this often; a map needs no more.
#define NATIVE_SECOND_SCREEN_MAX_FPS 60
#define NATIVE_SECOND_SCREEN_IDENTIFY_MS 3000
#define NATIVE_SECOND_SCREEN_DISPLAY_NAME_MAX 96

struct NativeSecondScreenMapCache
{
	int levelID;
	const struct Icon *top;
	const struct Icon *bottom;
	int topHalf;
	u32 *pixels;
	int version;
	int width;
	int height;
	// Map rectangle in HUD coordinates.
	int hudLeft;
	int hudTop;
	// Half the dot icon, so dots are placed by their centre.
	int dotHalfW;
	int dotHalfH;
	int pending;
};

global_variable struct NativeSecondScreenMapCache s_map = {-1};

// Lap log: retail keeps lap times only as on-screen digits, so the module times laps itself.
struct NativeSecondScreenLapLog
{
	int lastLap;
	int lapStart;
	int laps[NATIVE_SECOND_SCREEN_LAPS];
};

global_variable struct NativeSecondScreenLapLog s_lapLog[NATIVE_SECOND_SCREEN_DRIVERS];
CTR_STATIC_ASSERT(NATIVE_SECOND_SCREEN_DRIVERS >= NATIVE_MAX_DRIVERS);
global_variable int s_lapLogLevel = -1;

global_variable SDL_Window *s_window;
global_variable SDL_GLContext s_context;
global_variable SDL_DisplayID s_windowDisplay;
global_variable u64 s_lastPresentTicks;
global_variable u64 s_identifyEndTicks;
global_variable char s_displayName[NATIVE_SECOND_SCREEN_DISPLAY_NAME_MAX];

// ---- snapshot ---------------------------------------------------------------

internal const struct UIMapSpawnMetadata *NativeSecondScreen_MapMetadata(struct GameTracker *gGT)
{
	if ((gGT->level1 == NULL) || (gGT->level1->ptrSpawnType1 == NULL) || (gGT->level1->ptrSpawnType1->count == 0))
	{
		return NULL;
	}
	void **pointers = ST1_GETPOINTERS(gGT->level1->ptrSpawnType1);
	return (const struct UIMapSpawnMetadata *)pointers[ST1_MAP];
}

// UI_Map_GetIconPos without the 3P offset, then into 0..1 over the map image.
internal int NativeSecondScreen_MapPos(const struct UIMap *map, const s32 worldPos[3], float *outX, float *outY)
{
	const s32 worldRangeX = map->worldEndX - map->worldStartX;
	const s32 worldRangeY = map->worldEndY - map->worldStartY;
	if ((worldRangeX == 0) || (worldRangeY == 0) || (s_map.width <= 0) || (s_map.height <= 0))
	{
		return 0;
	}

	const s32 x = worldPos[0];
	const s32 y = worldPos[2];
	const s32 originY = map->iconStartY - NATIVE_MAP_ICON_Y_OFFSET;
	s32 screenX;
	s32 screenY;
	switch (map->mode)
	{
	case 0:
		screenX = map->iconStartX + (s32)((u32)x * map->iconSizeX) / worldRangeX;
		screenY = originY + (s32)((u32)y * map->iconSizeY * 2) / worldRangeY;
		break;
	case 1:
		screenY = originY + (s32)((u32)x * map->iconSizeY * 2) / worldRangeX;
		screenX = map->iconStartX - (s32)((u32)y * map->iconSizeX) / worldRangeY;
		break;
	case 2:
		screenX = map->iconStartX - (s32)((u32)x * map->iconSizeX) / worldRangeX;
		screenY = originY - (s32)((u32)y * map->iconSizeY * 2) / worldRangeY;
		break;
	default:
		screenY = originY - (s32)((u32)x * map->iconSizeY * 2) / worldRangeX;
		screenX = map->iconStartX + (s32)((u32)y * map->iconSizeX) / worldRangeY;
		break;
	}

	*outX = (float)(screenX + s_map.dotHalfW - s_map.hudLeft) / (float)s_map.width;
	*outY = (float)(screenY + s_map.dotHalfH - s_map.hudTop) / (float)s_map.height;
	return 1;
}

// Copies a game (LNG) string, or "" if there is none.
internal void NativeSecondScreen_CopyLng(char *dst, size_t size, int index)
{
	dst[0] = '\0';
	if ((sdata->lngStrings != NULL) && (index > 0) && (sdata->lngStrings[index] != NULL))
	{
		snprintf(dst, size, "%s", sdata->lngStrings[index]);
	}
}

internal void NativeSecondScreen_ResetLapLog(int levelID)
{
	SDL_zeroa(s_lapLog);
	s_lapLogLevel = levelID;
}

internal void NativeSecondScreen_UpdateLapLog(struct GameTracker *gGT, const struct Driver *d, struct NativeSecondScreenDriver *out)
{
	struct NativeSecondScreenLapLog *log = &s_lapLog[out->id];
	const int lap = (d->lapIndex == 0xff) ? 0 : d->lapIndex;
	while ((log->lastLap < lap) && (log->lastLap < NATIVE_SECOND_SCREEN_LAPS))
	{
		// lapTime is the clock at the start of the current lap.
		const int lapEnd = (d->lapTime > log->lapStart) ? d->lapTime : gGT->elapsedEventTime;
		log->laps[log->lastLap] = lapEnd - log->lapStart;
		log->lapStart = lapEnd;
		log->lastLap++;
	}
	if (lap < log->lastLap)
	{
		// Restarted.
		SDL_zerop(log);
	}
	SDL_memcpy(out->lapTimes, log->laps, sizeof(out->lapTimes));
	out->currentLapTime = out->finished ? 0 : (gGT->elapsedEventTime - log->lapStart);
}

internal void NativeSecondScreen_AddMarker(const struct UIMap *map, int type, const s32 worldPos[3])
{
	struct NativeSecondScreenState *state = &g_secondScreenState;
	if (state->markerCount >= NATIVE_SECOND_SCREEN_MARKERS)
	{
		return;
	}
	struct NativeSecondScreenMapMarker *marker = &state->markers[state->markerCount];
	if (NativeSecondScreen_MapPos(map, worldPos, &marker->mapX, &marker->mapY))
	{
		marker->type = type;
		state->markerCount++;
	}
}

internal void NativeSecondScreen_OnBeforeDraw(struct GameTracker *gGT, struct GamepadSystem *gGamepads)
{
	(void)gGamepads;
	struct NativeSecondScreenState *state = &g_secondScreenState;
	SDL_zerop(state);

	if (g_settings.secondScreen.mode == NATIVE_SECOND_SCREEN_OFF)
	{
		return;
	}

	state->timeTrial = (gGT->gameMode1 & TIME_TRIAL) != 0;
	state->relicRace = (gGT->gameMode1 & RELIC_RACE) != 0;
	state->battle = (gGT->gameMode1 & BATTLE_MODE) != 0;
	state->levelID = gGT->levelID;
	state->numPlayers = gGT->numPlyrCurrGame;
	state->numLaps = gGT->numLaps;
	state->raceTime = gGT->elapsedEventTime;
	if ((gGT->levelID >= 0) && (gGT->levelID < (int)SDL_arraysize(data.metaDataLEV)))
	{
		NativeSecondScreen_CopyLng(state->levelName, sizeof(state->levelName), data.metaDataLEV[gGT->levelID].name_LNG);
	}
	state->inRace = ((gGT->gameMode1 & (MAIN_MENU | LOADING | GAME_CUTSCENE | ADVENTURE_ARENA)) == 0) && ((gGT->hudFlags & HUD_FLAG_RACE_HUD) != 0) &&
	                (gGT->drivers[0] != NULL);
	if (!state->inRace)
	{
		return;
	}

	if ((s_lapLogLevel != gGT->levelID) || (gGT->elapsedEventTime == 0))
	{
		NativeSecondScreen_ResetLapLog(gGT->levelID);
	}

	// The map needs VRAM, read after the frame is presented.
	const struct UIMapSpawnMetadata *meta = NativeSecondScreen_MapMetadata(gGT);
	if ((meta != NULL) && ((s_map.levelID != gGT->levelID) || (s_map.top != gGT->ptrIcons[3]) || (s_map.bottom != gGT->ptrIcons[4])))
	{
		s_map.pending = 1;
	}
	const int mapReady = (meta != NULL) && (s_map.levelID == gGT->levelID) && (s_map.pixels != NULL) && !s_map.pending;
	state->mapReady = mapReady;

	// GAME_DRIVER_CAP is 24 in a big-field race, else 8.
	for (int i = 0; i < GAME_DRIVER_CAP; i++)
	{
		const struct Driver *d = GAME_DRIVER(gGT, i);
		if ((d == NULL) || (d->instSelf == NULL))
		{
			continue;
		}
		struct NativeSecondScreenDriver *out = &state->drivers[state->driverCount++];
		out->id = i;
		out->human = (d->actionsFlagSet & ACTION_BOT) == 0;
		out->characterId = (u16)GAME_DRIVER_CHARACTER_ID(d->driverID) & 0xf;
		out->colour = data.colors[CRASH_BLUE + out->characterId][0] & 0xffffff;
		NativeSecondScreen_CopyLng(out->name, sizeof(out->name), GAME_CHARACTER_METADATA[out->characterId].name_LNG_short);
		out->rank = d->driverRank;
		out->finished = (d->actionsFlagSet & ACTION_RACE_FINISHED) != 0;
		out->lap = (d->lapIndex == 0xff) ? 0 : d->lapIndex;
		out->wumpa = d->numWumpas;
		out->heldItem = d->heldItemID;
		out->heldCount = d->numHeldItems;
		out->speed = d->speedApprox;
		if (mapReady)
		{
			out->onMap = NativeSecondScreen_MapPos(&meta->map, d->instSelf->matrix.t, &out->mapX, &out->mapY);
		}
		NativeSecondScreen_UpdateLapLog(gGT, d, out);
	}

	int ordered = 0;
	for (int i = 0; (i < GAME_DRIVER_CAP) && (ordered < state->driverCount); i++)
	{
		const struct Driver *d = GAME_DRIVER_IN_RACE_ORDER(gGT, i);
		for (int k = 0; (d != NULL) && (k < state->driverCount); k++)
		{
			if (GAME_DRIVER(gGT, state->drivers[k].id) == d)
			{
				state->raceOrder[ordered++] = k;
				break;
			}
		}
	}
	// Anything not in the race order (battle) follows in driver order.
	for (int k = 0; (k < state->driverCount) && (ordered < state->driverCount); k++)
	{
		int listed = 0;
		for (int j = 0; j < ordered; j++)
		{
			listed |= state->raceOrder[j] == k;
		}
		if (!listed)
		{
			state->raceOrder[ordered++] = k;
		}
	}

	if (!mapReady)
	{
		return;
	}
	for (struct Thread *t = gGT->threadBuckets[GHOST].thread; t != NULL; t = t->siblingThread)
	{
		const struct Driver *d = t->object;
		if ((d != NULL) && (t->inst != NULL) && (d->ghostBoolInit != 0))
		{
			NativeSecondScreen_AddMarker(&meta->map, (d->ghostID != 0) ? NATIVE_SECOND_SCREEN_MARKER_STAFF_GHOST : NATIVE_SECOND_SCREEN_MARKER_GHOST,
			                             t->inst->matrix.t);
		}
	}
	for (struct Thread *t = gGT->threadBuckets[TRACKING].thread; t != NULL; t = t->siblingThread)
	{
		if ((t->inst != NULL) && (t->inst->model != NULL) && (t->inst->model->id == DYNAMIC_WARPBALL))
		{
			NativeSecondScreen_AddMarker(&meta->map, NATIVE_SECOND_SCREEN_MARKER_WARPBALL, t->inst->matrix.t);
		}
	}
}

// ---- map texture --------------------------------------------------------------

// A PS1 colour as RGBA; colour 0 is transparent. The map is drawn additively, so its
// brightness becomes alpha over the panel's dark background.
internal u32 NativeSecondScreen_MapColour(u16 colour)
{
	u32 r = (colour & 0x1f) * 255 / 31;
	u32 g = ((colour >> 5) & 0x1f) * 255 / 31;
	u32 b = ((colour >> 10) & 0x1f) * 255 / 31;
	const u32 a = SDL_max(r, SDL_max(g, b));
	if (a == 0)
	{
		return 0;
	}
	r = r * 255 / a;
	g = g * 255 / a;
	b = b * 255 / a;
	return r | (g << 8) | (b << 16) | (a << 24);
}

// Decodes one icon (4-, 8- or 15-bit texture page) into pixels at dstX, dstY. Reads only
// the icon's texels and its CLUT from VRAM.
internal void NativeSecondScreen_BlitIcon(u32 *pixels, int width, const struct Icon *icon, int dstX, int dstY)
{
	const struct TextureLayout *tex = &icon->texLayout;
	const int pageX = (tex->tpage & 0xf) * 64;
	const int pageY = ((tex->tpage >> 4) & 1) * 256;
	const int mode = (tex->tpage >> 7) & 3;
	const int shift = (mode == 0) ? 2 : (mode == 1) ? 1 : 0;
	const int w = tex->u1 - tex->u0;
	const int h = tex->v2 - tex->v0;
	if ((w <= 0) || (h <= 0))
	{
		return;
	}

	// Texture words covering u0..u1 on each of the icon's rows.
	const int wordX = pageX + (tex->u0 >> shift);
	const int words = SDL_min(((tex->u1 - 1) >> shift) - (tex->u0 >> shift) + 1, VRAM_WIDTH - wordX);
	const int rowY = pageY + tex->v0;
	const int rows = SDL_min(h, VRAM_HEIGHT - rowY);
	u16 *texels = calloc((size_t)words * rows, sizeof(u16));
	u16 clut[256];
	SDL_zeroa(clut);
	if (texels == NULL)
	{
		return;
	}
	NativeRenderer_ReadVRAM(texels, wordX, rowY, words, rows);
	if (mode < 2)
	{
		const int clutX = (tex->clut & 0x3f) * 16;
		const int clutY = (tex->clut >> 6) & 0x1ff;
		NativeRenderer_ReadVRAM(clut, clutX, clutY, SDL_min((mode == 0) ? 16 : 256, VRAM_WIDTH - clutX), 1);
	}

	for (int y = 0; y < rows; y++)
	{
		const u16 *row = texels + y * words;
		for (int x = 0; x < w; x++)
		{
			const int u = tex->u0 + x;
			const int word = (u >> shift) - (tex->u0 >> shift);
			if (word >= words)
			{
				break;
			}
			u16 colour;
			if (mode == 0)
			{
				colour = clut[(row[word] >> ((u & 3) * 4)) & 0xf];
			}
			else if (mode == 1)
			{
				colour = clut[(row[word] >> ((u & 1) * 8)) & 0xff];
			}
			else
			{
				colour = row[word];
			}
			pixels[(dstY + y) * width + dstX + x] = (colour != 0) ? NativeSecondScreen_MapColour(colour) : 0;
		}
	}
	free(texels);
}

internal void NativeSecondScreen_DecodeMap(struct GameTracker *gGT)
{
	s_map.pending = 0;
	const struct UIMapSpawnMetadata *meta = NativeSecondScreen_MapMetadata(gGT);
	const struct Icon *top = gGT->ptrIcons[3];
	const struct Icon *bottom = gGT->ptrIcons[4];
	s_map.levelID = gGT->levelID;
	s_map.top = top;
	s_map.bottom = bottom;
	s_map.width = 0;
	s_map.height = 0;
	free(s_map.pixels);
	s_map.pixels = NULL;
	s_map.version++;
	if ((meta == NULL) || (bottom == NULL))
	{
		return;
	}

	// Retail stacks the top half over the bottom half, both right-aligned (UI_Map_DrawMap).
	s_map.topHalf = (meta->topHalfMode == 0) && (top != NULL);
	const int bottomW = bottom->texLayout.u1 - bottom->texLayout.u0;
	const int bottomH = bottom->texLayout.v2 - bottom->texLayout.v0;
	const int topW = s_map.topHalf ? (top->texLayout.u1 - top->texLayout.u0) : 0;
	const int topH = s_map.topHalf ? (top->texLayout.v2 - top->texLayout.v0) : 0;
	const int width = SDL_max(topW, bottomW);
	const int height = topH + bottomH;
	if ((width <= 0) || (height <= 0) || (width > 256) || (height > 512))
	{
		return;
	}

	s_map.pixels = calloc((size_t)width * height, sizeof(u32));
	if (s_map.pixels == NULL)
	{
		return;
	}
	if (s_map.topHalf)
	{
		NativeSecondScreen_BlitIcon(s_map.pixels, width, top, width - topW, 0);
	}
	NativeSecondScreen_BlitIcon(s_map.pixels, width, bottom, width - bottomW, topH);

	s_map.width = width;
	s_map.height = height;
	s_map.hudLeft = NATIVE_MAP_HUD_RIGHT - width;
	s_map.hudTop = NATIVE_MAP_HUD_BOTTOM - height;

	const struct IconGroup *group = gGT->iconGroup[NATIVE_MAP_ICON_GROUP];
	const struct Icon *dot = NULL;
	if ((group != NULL) && (group->numIcons > NATIVE_MAP_DOT_ICON))
	{
		struct Icon **icons = ICONGROUP_GETICONS(group);
		dot = icons[NATIVE_MAP_DOT_ICON];
	}
	s_map.dotHalfW = (dot != NULL) ? (dot->texLayout.u1 - dot->texLayout.u0) / 2 : 4;
	s_map.dotHalfH = (dot != NULL) ? (dot->texLayout.v2 - dot->texLayout.v0) / 2 : 4;
}

const unsigned char *NativeSecondScreen_GetMapPixels(int *width, int *height, int *version)
{
	*width = s_map.width;
	*height = s_map.height;
	*version = s_map.version;
	return (const unsigned char *)s_map.pixels;
}

internal void NativeSecondScreen_OnAfterPresent(struct GameTracker *gGT)
{
	if ((g_settings.secondScreen.mode != NATIVE_SECOND_SCREEN_OFF) && s_map.pending)
	{
		NativeSecondScreen_DecodeMap(gGT);
	}
}

// ---- main screen minimap ------------------------------------------------------

int NativeSecondScreen_IsLive(void)
{
	switch (g_settings.secondScreen.mode)
	{
	case NATIVE_SECOND_SCREEN_INSET:
		return 1;
	case NATIVE_SECOND_SCREEN_DISPLAY:
		return s_window != NULL;
	default:
		return 0;
	}
}

internal void NativeSecondScreen_OnHudMinimap(int *draw)
{
	if (g_settings.secondScreen.hideMainMinimap && !g_settings.global.ps1Mode && NativeSecondScreen_IsLive())
	{
		*draw = 0;
	}
}

void NativeSecondScreen_RegisterHooks(void)
{
	NativeHook_RegisterBeforeDraw(NativeSecondScreen_OnBeforeDraw);
	NativeHook_RegisterAfterPresent(NativeSecondScreen_OnAfterPresent);
	NativeHook_RegisterHudMinimap(NativeSecondScreen_OnHudMinimap);
}

// ---- displays -----------------------------------------------------------------

// 1-based display index -> SDL display, or 0.
internal SDL_DisplayID NativeSecondScreen_DisplayAt(int index)
{
	int count = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&count);
	SDL_DisplayID id = 0;
	if ((displays != NULL) && (index >= 1) && (index <= count))
	{
		id = displays[index - 1];
	}
	SDL_free(displays);
	return id;
}

internal int NativeSecondScreen_IndexOf(SDL_DisplayID display)
{
	int count = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&count);
	int index = 0;
	for (int i = 0; (displays != NULL) && (i < count); i++)
	{
		if (displays[i] == display)
		{
			index = i + 1;
		}
	}
	SDL_free(displays);
	return index;
}

int NativeSecondScreen_DisplayCount(void)
{
	int count = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&count);
	SDL_free(displays);
	return count;
}

const char *NativeSecondScreen_DisplayName(int index)
{
	const SDL_DisplayID display = NativeSecondScreen_DisplayAt(index);
	if (display == 0)
	{
		return "";
	}
	const char *name = SDL_GetDisplayName(display);
	SDL_Rect bounds = {0, 0, 0, 0};
	SDL_GetDisplayBounds(display, &bounds);
	const int isMain = (g_window != NULL) && (SDL_GetDisplayForWindow(g_window) == display);
	snprintf(s_displayName, sizeof(s_displayName), "%d: %s (%dx%d)%s", index, ((name != NULL) && (name[0] != '\0')) ? name : "Display", bounds.w,
	         bounds.h, isMain ? " - game" : "");
	return s_displayName;
}

int NativeSecondScreen_GameDisplay(void)
{
	return (g_window != NULL) ? NativeSecondScreen_IndexOf(SDL_GetDisplayForWindow(g_window)) : 0;
}

int NativeSecondScreen_CurrentDisplay(void)
{
	return (s_window != NULL) ? NativeSecondScreen_IndexOf(s_windowDisplay) : 0;
}

void NativeSecondScreen_Identify(void)
{
	s_identifyEndTicks = SDL_GetTicks() + NATIVE_SECOND_SCREEN_IDENTIFY_MS;
}

float NativeSecondScreen_IdentifyTimeLeft(void)
{
	const u64 now = SDL_GetTicks();
	return (now < s_identifyEndTicks) ? (float)(s_identifyEndTicks - now) / 1000.0f : 0.0f;
}

// The display the second window belongs on: the chosen one, else the first that is not the game's.
internal SDL_DisplayID NativeSecondScreen_TargetDisplay(void)
{
	const SDL_DisplayID mainDisplay = (g_window != NULL) ? SDL_GetDisplayForWindow(g_window) : 0;
	if (g_settings.secondScreen.display > 0)
	{
		const SDL_DisplayID chosen = NativeSecondScreen_DisplayAt(g_settings.secondScreen.display);
		return (chosen != mainDisplay) ? chosen : 0;
	}

	int count = 0;
	SDL_DisplayID *displays = SDL_GetDisplays(&count);
	SDL_DisplayID target = 0;
	for (int i = 0; (displays != NULL) && (i < count) && (target == 0); i++)
	{
		if (displays[i] != mainDisplay)
		{
			target = displays[i];
		}
	}
	SDL_free(displays);
	return target;
}

// ---- second window ------------------------------------------------------------

internal void NativeSecondScreen_CloseWindow(void)
{
	if (s_window == NULL)
	{
		return;
	}
	SDL_GLContext mainContext = SDL_GL_GetCurrentContext();
	if (s_context != NULL)
	{
		SDL_GL_MakeCurrent(s_window, s_context);
		NativeSecondScreenUI_Shutdown();
		SDL_GL_MakeCurrent(g_window, mainContext);
		SDL_GL_DestroyContext(s_context);
		s_context = NULL;
	}
	SDL_DestroyWindow(s_window);
	s_window = NULL;
	s_windowDisplay = 0;
	Platform_Log("[CTR Native] Second screen closed\n");
}

internal void NativeSecondScreen_OpenWindow(SDL_DisplayID display)
{
	SDL_Rect bounds = {0, 0, 640, 480};
	SDL_GetDisplayBounds(display, &bounds);

	SDL_GLContext mainContext = SDL_GL_GetCurrentContext();
	char title[64];
	snprintf(title, sizeof(title), "%s - Second Screen", NATIVE_PRODUCT_NAME);
	// Not focusable, so the game window keeps the keyboard; taps still arrive.
	s_window = SDL_CreateWindow(title, bounds.w, bounds.h, SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_NOT_FOCUSABLE);
	if (s_window == NULL)
	{
		Platform_LogWarn("[CTR Native] Second screen: cannot open a window (%s)\n", SDL_GetError());
		return;
	}
	SDL_SetWindowPosition(s_window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
	SDL_SetWindowFullscreen(s_window, true);
	SDL_SyncWindow(s_window);

	// Shared with the game's context: the map texture and ImGui's font atlas and shaders.
	SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
	s_context = SDL_GL_CreateContext(s_window);
	SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
	if (s_context == NULL)
	{
		Platform_LogWarn("[CTR Native] Second screen: cannot create a GL context (%s)\n", SDL_GetError());
		SDL_GL_MakeCurrent(g_window, mainContext);
		SDL_DestroyWindow(s_window);
		s_window = NULL;
		return;
	}
	// The game window paces the frame; this one never waits for vsync.
	SDL_GL_SetSwapInterval(0);
	SDL_GL_MakeCurrent(g_window, mainContext);

	s_windowDisplay = display;
	Platform_Log("[CTR Native] Second screen on display %d (%dx%d)\n", NativeSecondScreen_IndexOf(display), bounds.w, bounds.h);
}

void NativeSecondScreen_Apply(void)
{
	if (g_window == NULL)
	{
		return;
	}
	if (g_settings.secondScreen.mode != NATIVE_SECOND_SCREEN_DISPLAY)
	{
		NativeSecondScreen_CloseWindow();
		return;
	}

	const SDL_DisplayID target = NativeSecondScreen_TargetDisplay();
	if ((s_window != NULL) && (target == s_windowDisplay))
	{
		return;
	}
	NativeSecondScreen_CloseWindow();
	if (target != 0)
	{
		NativeSecondScreen_OpenWindow(target);
	}
}

void NativeSecondScreen_Shutdown(void)
{
	NativeSecondScreen_CloseWindow();
	free(s_map.pixels);
	s_map.pixels = NULL;
	s_map.version++;
}

void NativeSecondScreen_Present(void)
{
	if (s_window == NULL)
	{
		return;
	}
	const u64 now = SDL_GetTicks();
	if ((now - s_lastPresentTicks) < (1000 / NATIVE_SECOND_SCREEN_MAX_FPS))
	{
		return;
	}
	s_lastPresentTicks = now;

	SDL_GLContext mainContext = SDL_GL_GetCurrentContext();
	// Uploads made in the game's context (map, font atlas) must reach the shared one.
	glFlush();
	if (!SDL_GL_MakeCurrent(s_window, s_context))
	{
		return;
	}

	int width = 0;
	int height = 0;
	int pixelWidth = 0;
	int pixelHeight = 0;
	SDL_GetWindowSize(s_window, &width, &height);
	SDL_GetWindowSizeInPixels(s_window, &pixelWidth, &pixelHeight);
	if ((width > 0) && (height > 0) && (pixelWidth > 0) && (pixelHeight > 0))
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, pixelWidth, pixelHeight);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		const int identify = (NativeSecondScreen_IdentifyTimeLeft() > 0.0f) ? NativeSecondScreen_IndexOf(s_windowDisplay) : 0;
		NativeSecondScreenUI_RenderWindow(width, height, pixelWidth, pixelHeight, identify);
		SDL_GL_SwapWindow(s_window);
	}
	SDL_GL_MakeCurrent(g_window, mainContext);
}

int NativeSecondScreen_ProcessEvent(const SDL_Event *event)
{
	if (event->type == SDL_EVENT_DISPLAY_ADDED || event->type == SDL_EVENT_DISPLAY_REMOVED)
	{
		// A display came or went: reopen on the right one, or close.
		if (event->type == SDL_EVENT_DISPLAY_REMOVED && event->display.displayID == s_windowDisplay)
		{
			NativeSecondScreen_CloseWindow();
		}
		NativeSecondScreen_Apply();
		return 0;
	}

	if (s_window == NULL)
	{
		return 0;
	}
	const SDL_WindowID id = SDL_GetWindowID(s_window);
	SDL_WindowID eventWindow = 0;
	if ((event->type >= SDL_EVENT_WINDOW_FIRST) && (event->type <= SDL_EVENT_WINDOW_LAST))
	{
		eventWindow = event->window.windowID;
		if ((eventWindow == id) && (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED))
		{
			// Closing the second window turns the second screen off, not the game.
			g_settings.secondScreen.mode = NATIVE_SECOND_SCREEN_OFF;
			NativeSecondScreen_CloseWindow();
			NativeSettings_Save();
			return 1;
		}
	}
	else if ((event->type == SDL_EVENT_MOUSE_MOTION) || (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) || (event->type == SDL_EVENT_MOUSE_BUTTON_UP) ||
	         (event->type == SDL_EVENT_MOUSE_WHEEL))
	{
		// Same windowID offset for motion, button and wheel events.
		eventWindow = event->motion.windowID;
		if (eventWindow == id)
		{
			NativeSecondScreenUI_ProcessEvent(event);
		}
	}
	return (eventWindow != 0) && (eventWindow == id);
}
