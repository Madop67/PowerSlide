#ifndef NATIVE_SECOND_SCREEN_H
#define NATIVE_SECOND_SCREEN_H

// Dual Screen: a DS-style second screen (track map, race info, lap times) for
// dual-screen handhelds and second monitors, or as an inset in the main window.
//
// platform/native_second_screen.c reads the game each tick (read-only, so replays
// are unaffected), owns the second window and the map texture, and hides the
// main screen's minimap through the HudMinimap hook. The pages are drawn with
// Dear ImGui in platform/native_second_screen_ui.cpp.
//
// NOTE: Shared with the C++ UI layer; keep to plain C types.

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// Room for a big-field race (NATIVE_MAX_DRIVERS).
#define NATIVE_SECOND_SCREEN_DRIVERS 24
#define NATIVE_SECOND_SCREEN_LAPS    7
#define NATIVE_SECOND_SCREEN_MARKERS 4
// Race clock units per second (the game's SECOND).
#define NATIVE_SECOND_SCREEN_CLOCK_SECOND 960
// HELD_ITEM_NONE.
#define NATIVE_SECOND_SCREEN_NO_ITEM 0xf

enum NativeSecondScreenMarker
{
	NATIVE_SECOND_SCREEN_MARKER_GHOST = 0,
	NATIVE_SECOND_SCREEN_MARKER_STAFF_GHOST,
	NATIVE_SECOND_SCREEN_MARKER_WARPBALL,
};

struct NativeSecondScreenDriver
{
	// Index into drivers[]; human players are 0..numPlayers-1.
	int id;
	int human;
	int characterId;
	// The character's map-dot colour, 0xBBGGRR.
	unsigned int colour;
	char name[24];
	// 0 = first.
	int rank;
	// Laps completed; laps[0..lap-1] are filled for humans.
	int lap;
	int finished;
	int wumpa;
	// HELD_ITEM_*, or NATIVE_SECOND_SCREEN_NO_ITEM.
	int heldItem;
	int heldCount;
	int speed;
	// Position on the map image, 0..1 from its top-left corner; onMap is 0 if unknown.
	int onMap;
	float mapX;
	float mapY;
	// Race clock units.
	int lapTimes[NATIVE_SECOND_SCREEN_LAPS];
	int currentLapTime;
};

struct NativeSecondScreenMapMarker
{
	int type;
	float mapX;
	float mapY;
};

// What the pages show, refreshed once per game tick.
struct NativeSecondScreenState
{
	// A race (or battle) is running with its HUD up.
	int inRace;
	int timeTrial;
	int relicRace;
	int battle;
	int levelID;
	int numPlayers;
	int numLaps;
	// Race clock units.
	int raceTime;
	char levelName[48];

	// The level's minimap has been decoded (see NativeSecondScreen_GetMapPixels).
	int mapReady;

	int driverCount;
	struct NativeSecondScreenDriver drivers[NATIVE_SECOND_SCREEN_DRIVERS];
	// drivers[] indices in race order.
	int raceOrder[NATIVE_SECOND_SCREEN_DRIVERS];
	int markerCount;
	struct NativeSecondScreenMapMarker markers[NATIVE_SECOND_SCREEN_MARKERS];
};

extern struct NativeSecondScreenState g_secondScreenState;

// The level's minimap as RGBA pixels (top-left first), or NULL. version changes whenever
// the pixels do.
const unsigned char *NativeSecondScreen_GetMapPixels(int *width, int *height, int *version);

// Opens, moves or closes the second window to match g_settings.secondScreen.
void NativeSecondScreen_Apply(void);
void NativeSecondScreen_Shutdown(void);
// Draws and presents the second window, if open. Called after the main window's swap.
void NativeSecondScreen_Present(void);
// Takes the second window's events. Returns 1 if the event belonged to it.
int NativeSecondScreen_ProcessEvent(const SDL_Event *event);
// Whether the second screen currently shows (a window is open, or the inset is on).
int NativeSecondScreen_IsLive(void);

// Displays for the menu: count, and a name per 1-based index. The main window's display is marked.
int NativeSecondScreen_DisplayCount(void);
const char *NativeSecondScreen_DisplayName(int index);
// 1-based index of the display the second window is on, or 0 if none is open.
int NativeSecondScreen_CurrentDisplay(void);
// 1-based index of the game window's display.
int NativeSecondScreen_GameDisplay(void);
// Shows the display number on each screen for a few seconds.
void NativeSecondScreen_Identify(void);
// Seconds left of an Identify, or 0.
float NativeSecondScreen_IdentifyTimeLeft(void);

// C++ side (native_second_screen_ui.cpp).
// Renders the pages into the bound default framebuffer of a width x height window
// (pixelWidth x pixelHeight pixels), with its own ImGui context. identifyNumber > 0
// overlays that display number.
void NativeSecondScreenUI_RenderWindow(int width, int height, int pixelWidth, int pixelHeight, int identifyNumber);
// Mouse and touch events already mapped to the second window.
void NativeSecondScreenUI_ProcessEvent(const SDL_Event *event);
void NativeSecondScreenUI_Shutdown(void);

#ifdef __cplusplus
}

// Main ImGui context, between frames: uploads the map and logo textures when they changed.
void NativeSecondScreenUI_UpdateTextures();
// Main ImGui context, before its renderer shuts down.
void NativeSecondScreenUI_DestroyTextures();
// Draws the inset panel inside the main window's ImGui frame.
void NativeSecondScreenUI_DrawInset();
// Draws the display number over the main window while an Identify runs.
void NativeSecondScreenUI_DrawIdentify(int displayNumber);
#endif

#endif
