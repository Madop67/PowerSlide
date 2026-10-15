// Dual Screen pages (see native_second_screen.h), drawn with Dear ImGui in the
// settings menu's CTR colours: a track map with every kart, a race standings page and
// a lap times page, plus an idle card outside races. The same panel is drawn into the
// second window (its own ImGui context, sharing the menu's font atlas) or as an inset
// in the main window.

#include <platform/native_product.h>
#include <platform/native_second_screen.h>
#include <platform/native_settings.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

namespace
{

// Same CTR palette as the settings menu (native_ui.cpp).
constexpr ImU32 Rgb(unsigned r, unsigned g, unsigned b, unsigned a = 255)
{
	return IM_COL32(r, g, b, a);
}

constexpr ImU32 kNavy = Rgb(0x12, 0x18, 0x3A);
constexpr ImU32 kDeepBlue = Rgb(0x1B, 0x2A, 0x78);
constexpr ImU32 kBlue = Rgb(0x23, 0x40, 0xA8);
constexpr ImU32 kFrame = Rgb(0x1E, 0x2A, 0x63);
constexpr ImU32 kOrange = Rgb(0xE8, 0x73, 0x1A);
constexpr ImU32 kOrangeLight = Rgb(0xF5, 0x9A, 0x2A);
constexpr ImU32 kYellow = Rgb(0xFF, 0xD2, 0x1F);
constexpr ImU32 kMutedText = Rgb(0x9A, 0xA4, 0xD8);
constexpr ImU32 kWhite = Rgb(0xFF, 0xFF, 0xFF);
constexpr ImU32 kGreen = Rgb(0x4C, 0xD9, 0x64);
constexpr ImU32 kRed = Rgb(0xF0, 0x4A, 0x3A);
constexpr ImU32 kGhostBlue = Rgb(0x8F, 0xC8, 0xFF);

enum Page
{
	kPageMap = 0,
	kPageRace,
	kPageTimes,
	kPageCount,
	kPageIdle = kPageCount,
};

constexpr const char *kPageNames[kPageCount] = {"MAP", "RACE", "TIMES"};

// HELD_ITEM_* names.
constexpr const char *kItemNames[] = {"Turbo",   "Bomb",     "Missile", "TNT",       "Potion",       "Spring",       "Shield", "Mask", "Clock",
                                      "Warpball", "3 Bombs", "3 Missiles", "Invisibility", "Super engine", "",       "",     "Roulette"};

// Map pixels are this many times wider than tall: HUD units are 512 x 216 shown at 4:3.
constexpr float kHudPixelAspect = (4.0f / 512.0f) / (3.0f / 216.0f);
// The sharp map is uploaded enlarged, so the GL backend's bilinear sampling keeps hard edges.
constexpr int kMapPixelScale = 4;
// Inset panel height, as a fraction of the main window's.
constexpr float kInsetHeight = 0.38f;
constexpr float kInsetAspect = 0.9f;

ImGuiContext *s_context = nullptr;
bool s_backendReady = false;
ImTextureData *s_mapTexture = nullptr;
int s_mapVersion = -1;
int s_mapScale = 0;
// Size of the decoded map, before kMapPixelScale.
int s_mapWidth = 0;
int s_mapHeight = 0;
// Page picked by tapping the page strip, until the race ends; -1 follows the setting.
int s_pageOverride = -1;
// Highest speed seen this race, for the speed bar.
int s_topSpeed = 1;

// ---- formatting ---------------------------------------------------------------

void FormatTime(char *out, size_t size, int units)
{
	if (units <= 0)
	{
		snprintf(out, size, "-:--.--");
		return;
	}
	const int hundredths = (int)((long long)units * 100 / NATIVE_SECOND_SCREEN_CLOCK_SECOND);
	snprintf(out, size, "%d:%02d.%02d", hundredths / 6000, (hundredths / 100) % 60, hundredths % 100);
}

void FormatDelta(char *out, size_t size, int units)
{
	const int hundredths = (int)((long long)(units < 0 ? -units : units) * 100 / NATIVE_SECOND_SCREEN_CLOCK_SECOND);
	snprintf(out, size, "%c%d.%02d", units < 0 ? '-' : '+', hundredths / 100, hundredths % 100);
}

const char *Ordinal(int rank)
{
	static const char *const kOrdinals[] = {"1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
	return ((rank >= 0) && (rank < 8)) ? kOrdinals[rank] : "-";
}

const char *ItemName(int item)
{
	return ((item >= 0) && (item < (int)IM_ARRAYSIZE(kItemNames))) ? kItemNames[item] : "";
}

// The character's dot colour (0xBBGGRR is ImGui's byte order).
ImU32 DriverColour(const NativeSecondScreenDriver &driver)
{
	return (ImU32)driver.colour | IM_COL32_A_MASK;
}

ImU32 WithAlpha(ImU32 colour, float alpha)
{
	return (colour & ~IM_COL32_A_MASK) | ((ImU32)(SDL_clamp(alpha, 0.0f, 1.0f) * 255.0f) << IM_COL32_A_SHIFT);
}

// ---- drawing helpers ----------------------------------------------------------

struct Panel
{
	ImDrawList *draw;
	ImFont *font;
	// Text unit: the body font size.
	float unit;
	double time;
};

ImVec2 TextSize(const Panel &panel, float size, const char *text)
{
	return panel.font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
}

// align: 0 = left, 0.5 = centre, 1 = right of pos.x.
void Text(const Panel &panel, float size, ImVec2 pos, ImU32 colour, const char *text, float align = 0.0f)
{
	if (align != 0.0f)
	{
		pos.x -= TextSize(panel, size, text).x * align;
	}
	panel.draw->AddText(panel.font, size, ImVec2(floorf(pos.x), floorf(pos.y)), colour, text);
}

// A small label over a big value, centred on centreX.
void Stat(const Panel &panel, float centreX, float y, const char *label, const char *value, ImU32 valueColour)
{
	Text(panel, panel.unit * 0.75f, ImVec2(centreX, y), kMutedText, label, 0.5f);
	Text(panel, panel.unit * 1.35f, ImVec2(centreX, y + panel.unit * 0.85f), valueColour, value, 0.5f);
}

const NativeSecondScreenDriver *PlayerOne(const NativeSecondScreenState &state)
{
	for (int i = 0; i < state.driverCount; i++)
	{
		if (state.drivers[i].human && (state.drivers[i].id == 0))
		{
			return &state.drivers[i];
		}
	}
	return nullptr;
}

int CurrentPage(const NativeSecondScreenState &state)
{
	if (!state.inRace)
	{
		return kPageIdle;
	}
	if (s_pageOverride >= 0)
	{
		return s_pageOverride;
	}
	switch (g_settings.secondScreen.startPage)
	{
	case NATIVE_SECOND_SCREEN_PAGE_MAP:
		return kPageMap;
	case NATIVE_SECOND_SCREEN_PAGE_RACE:
		return kPageRace;
	case NATIVE_SECOND_SCREEN_PAGE_TIMES:
		return state.battle ? kPageMap : kPageTimes;
	default:
		return (state.timeTrial || state.relicRace) ? kPageTimes : kPageMap;
	}
}

// ---- pages --------------------------------------------------------------------

void DrawMapPage(const Panel &panel, const NativeSecondScreenState &state, ImVec2 min, ImVec2 max)
{
	const float u = panel.unit;
	const NativeSecondScreenDriver *p1 = PlayerOne(state);

	// Info bar along the bottom.
	const float barHeight = u * 2.6f;
	const ImVec2 barMin(min.x, max.y - barHeight);
	panel.draw->AddRectFilled(barMin, max, kFrame, u * 0.4f);
	if ((state.numPlayers <= 1) && (p1 != nullptr))
	{
		char lap[16];
		char wumpa[8];
		snprintf(lap, sizeof(lap), "%d/%d", SDL_min(p1->lap + 1, state.numLaps), state.numLaps);
		snprintf(wumpa, sizeof(wumpa), "%d", p1->wumpa);
		const int columns = state.battle ? 3 : 4;
		const float column = (max.x - min.x) / (float)columns;
		const float y = barMin.y + u * 0.25f;
		int c = 0;
		Stat(panel, min.x + column * (c++ + 0.5f), y, "POS", Ordinal(p1->rank), kYellow);
		if (!state.battle)
		{
			Stat(panel, min.x + column * (c++ + 0.5f), y, "LAP", lap, kWhite);
		}
		Stat(panel, min.x + column * (c++ + 0.5f), y, "WUMPA", wumpa, (p1->wumpa >= 10) ? kOrangeLight : kWhite);
		const char *item = (p1->heldItem == NATIVE_SECOND_SCREEN_NO_ITEM) ? "-" : ItemName(p1->heldItem);
		Stat(panel, min.x + column * (c + 0.5f), y, "ITEM", item, kWhite);
	}
	else
	{
		// Split screen: one chip per player.
		int humans = 0;
		for (int i = 0; i < state.driverCount; i++)
		{
			humans += state.drivers[i].human;
		}
		const float column = (max.x - min.x) / (float)SDL_max(humans, 1);
		int c = 0;
		for (int i = 0; i < state.driverCount; i++)
		{
			const NativeSecondScreenDriver &d = state.drivers[i];
			if (!d.human)
			{
				continue;
			}
			char label[8];
			char value[24];
			snprintf(label, sizeof(label), "P%d", d.id + 1);
			if (state.battle)
			{
				snprintf(value, sizeof(value), "%s", Ordinal(d.rank));
			}
			else
			{
				snprintf(value, sizeof(value), "%s  %d/%d", Ordinal(d.rank), SDL_min(d.lap + 1, state.numLaps), state.numLaps);
			}
			Stat(panel, min.x + column * (c++ + 0.5f), barMin.y + u * 0.25f, label, value, DriverColour(d));
		}
	}

	// The map, as large as fits above the bar.
	const ImVec2 areaMin(min.x + u * 0.5f, min.y + u * 0.5f);
	const ImVec2 areaMax(max.x - u * 0.5f, barMin.y - u * 0.5f);
	if (!state.mapReady || (s_mapTexture == nullptr) || (s_mapTexture->Status != ImTextureStatus_OK) || (s_mapHeight <= 0))
	{
		Text(panel, u, ImVec2((areaMin.x + areaMax.x) * 0.5f, (areaMin.y + areaMax.y) * 0.5f - u * 0.5f), kMutedText, "No map for this track", 0.5f);
		return;
	}
	const float aspect = (float)s_mapWidth * kHudPixelAspect / (float)s_mapHeight;
	const float areaW = areaMax.x - areaMin.x;
	const float areaH = areaMax.y - areaMin.y;
	float mapW = areaW;
	float mapH = mapW / aspect;
	if (mapH > areaH)
	{
		mapH = areaH;
		mapW = mapH * aspect;
	}
	const ImVec2 mapMin(areaMin.x + (areaW - mapW) * 0.5f, areaMin.y + (areaH - mapH) * 0.5f);
	const ImVec2 mapMax(mapMin.x + mapW, mapMin.y + mapH);
	panel.draw->AddImage(ImTextureRef(s_mapTexture->GetTexID()), mapMin, mapMax);

	auto toScreen = [&](float x, float y) { return ImVec2(mapMin.x + x * mapW, mapMin.y + y * mapH); };
	const float radius = SDL_max(u * 0.3f, 3.0f);
	const float pulse = 0.5f + 0.5f * sinf((float)panel.time * 6.0f);

	for (int i = 0; i < state.markerCount; i++)
	{
		const NativeSecondScreenMapMarker &marker = state.markers[i];
		const ImVec2 p = toScreen(marker.mapX, marker.mapY);
		if (marker.type == NATIVE_SECOND_SCREEN_MARKER_WARPBALL)
		{
			panel.draw->AddCircleFilled(p, radius * 0.9f, WithAlpha(kRed, 0.6f + 0.4f * pulse));
		}
		else
		{
			const ImU32 colour = (marker.type == NATIVE_SECOND_SCREEN_MARKER_STAFF_GHOST) ? kGhostBlue : kWhite;
			panel.draw->AddCircle(p, radius, WithAlpha(colour, 0.85f), 0, SDL_max(u * 0.12f, 1.5f));
		}
	}

	// Bots first, then players on top.
	for (int pass = 0; pass < 2; pass++)
	{
		for (int i = 0; i < state.driverCount; i++)
		{
			const NativeSecondScreenDriver &d = state.drivers[i];
			if (!d.onMap || (d.human != (pass == 1)))
			{
				continue;
			}
			const ImVec2 p = toScreen(d.mapX, d.mapY);
			if (!d.human)
			{
				panel.draw->AddCircleFilled(p, radius, DriverColour(d));
				panel.draw->AddCircle(p, radius, Rgb(0, 0, 0, 160), 0, SDL_max(u * 0.06f, 1.0f));
				continue;
			}
			const float r = radius * 1.35f;
			panel.draw->AddCircle(p, r * (1.4f + 0.5f * pulse), WithAlpha(kWhite, 0.6f * (1.0f - pulse)), 0, SDL_max(u * 0.1f, 1.5f));
			panel.draw->AddCircleFilled(p, r, DriverColour(d));
			panel.draw->AddCircle(p, r, kWhite, 0, SDL_max(u * 0.14f, 2.0f));
			if (state.numPlayers > 1)
			{
				char label[4];
				snprintf(label, sizeof(label), "P%d", d.id + 1);
				Text(panel, u * 0.7f, ImVec2(p.x, p.y - r - u * 0.85f), kWhite, label, 0.5f);
			}
		}
	}
}

void DrawRacePage(const Panel &panel, const NativeSecondScreenState &state, ImVec2 min, ImVec2 max)
{
	const float u = panel.unit;
	const NativeSecondScreenDriver *p1 = PlayerOne(state);

	// Player card along the bottom.
	const float cardHeight = (p1 != nullptr) ? u * 3.4f : 0.0f;
	const float rowHeight = SDL_min(u * 1.5f, (max.y - min.y - cardHeight) / (float)SDL_max(state.driverCount, 1));
	float y = min.y;
	for (int i = 0; i < state.driverCount; i++, y += rowHeight)
	{
		const NativeSecondScreenDriver &d = state.drivers[state.raceOrder[i]];
		const ImVec2 rowMin(min.x, y + rowHeight * 0.06f);
		const ImVec2 rowMax(max.x, y + rowHeight * 0.94f);
		panel.draw->AddRectFilled(rowMin, rowMax, d.human ? kBlue : kFrame, u * 0.35f);
		const float textY = y + (rowHeight - u) * 0.5f;
		Text(panel, u, ImVec2(min.x + u * 0.5f, textY), (i == 0) ? kYellow : kWhite, Ordinal(state.battle ? i : d.rank));
		panel.draw->AddCircleFilled(ImVec2(min.x + u * 3.2f, y + rowHeight * 0.5f), u * 0.35f, DriverColour(d));
		Text(panel, u, ImVec2(min.x + u * 4.0f, textY), kWhite, (d.name[0] != '\0') ? d.name : "?");

		char right[24] = "";
		if (d.finished)
		{
			snprintf(right, sizeof(right), "FINISHED");
		}
		else if (!state.battle)
		{
			snprintf(right, sizeof(right), "LAP %d/%d", SDL_min(d.lap + 1, state.numLaps), state.numLaps);
		}
		if (d.human && (state.numPlayers > 1))
		{
			char tagged[32];
			snprintf(tagged, sizeof(tagged), "P%d  %s", d.id + 1, right);
			snprintf(right, sizeof(right), "%s", tagged);
		}
		Text(panel, u * 0.85f, ImVec2(max.x - u * 0.5f, y + (rowHeight - u * 0.85f) * 0.5f), d.human ? kYellow : kMutedText, right, 1.0f);
	}

	if (p1 == nullptr)
	{
		return;
	}
	const ImVec2 cardMin(min.x, max.y - cardHeight + u * 0.3f);
	panel.draw->AddRectFilled(cardMin, max, kDeepBlue, u * 0.4f);
	const float half = (max.x - min.x) * 0.5f;
	char item[32];
	if (p1->heldItem == NATIVE_SECOND_SCREEN_NO_ITEM)
	{
		snprintf(item, sizeof(item), "-");
	}
	else if (p1->heldCount > 1)
	{
		snprintf(item, sizeof(item), "%s x%d", ItemName(p1->heldItem), p1->heldCount);
	}
	else
	{
		snprintf(item, sizeof(item), "%s", ItemName(p1->heldItem));
	}
	char wumpa[16];
	snprintf(wumpa, sizeof(wumpa), (p1->wumpa >= 10) ? "%d JUICED" : "%d", p1->wumpa);
	Stat(panel, min.x + half * 0.5f, cardMin.y + u * 0.2f, "ITEM", item, kWhite);
	Stat(panel, min.x + half * 1.5f, cardMin.y + u * 0.2f, "WUMPA", wumpa, (p1->wumpa >= 10) ? kOrangeLight : kWhite);

	// Speed, relative to the fastest seen this race.
	s_topSpeed = SDL_max(s_topSpeed, abs(p1->speed));
	const float fill = SDL_clamp((float)abs(p1->speed) / (float)s_topSpeed, 0.0f, 1.0f);
	const ImVec2 barMin(cardMin.x + u * 0.6f, max.y - u * 0.75f);
	const ImVec2 barMax(max.x - u * 0.6f, max.y - u * 0.4f);
	panel.draw->AddRectFilled(barMin, barMax, kFrame, u);
	panel.draw->AddRectFilled(barMin, ImVec2(barMin.x + (barMax.x - barMin.x) * fill, barMax.y), (fill > 0.95f) ? kYellow : kOrange, u);
}

void DrawTimesPage(const Panel &panel, const NativeSecondScreenState &state, ImVec2 min, ImVec2 max)
{
	const float u = panel.unit;
	const NativeSecondScreenDriver *p1 = PlayerOne(state);
	if (p1 == nullptr)
	{
		return;
	}

	char text[32];
	const float centreX = (min.x + max.x) * 0.5f;
	const int lapCount = SDL_min(state.numLaps, NATIVE_SECOND_SCREEN_LAPS);
	if (!p1->finished)
	{
		snprintf(text, sizeof(text), "LAP %d", SDL_min(p1->lap + 1, state.numLaps));
		Text(panel, u * 0.85f, ImVec2(centreX, min.y), kMutedText, text, 0.5f);
		FormatTime(text, sizeof(text), p1->currentLapTime);
		Text(panel, u * 2.6f, ImVec2(centreX, min.y + u * 0.9f), kYellow, text, 0.5f);
	}
	else
	{
		Text(panel, u * 0.85f, ImVec2(centreX, min.y), kMutedText, "FINISHED", 0.5f);
		int total = 0;
		for (int i = 0; i < lapCount; i++)
		{
			total += p1->lapTimes[i];
		}
		FormatTime(text, sizeof(text), total);
		Text(panel, u * 2.6f, ImVec2(centreX, min.y + u * 0.9f), kYellow, text, 0.5f);
	}

	int best = 0;
	for (int i = 0; i < SDL_min(p1->lap, lapCount); i++)
	{
		if ((p1->lapTimes[i] > 0) && ((best == 0) || (p1->lapTimes[i] < best)))
		{
			best = p1->lapTimes[i];
		}
	}

	float y = min.y + u * 4.4f;
	const float rowHeight = SDL_min(u * 1.5f, (max.y - y - u * 1.8f) / (float)SDL_max(lapCount, 1));
	for (int i = 0; i < lapCount; i++, y += rowHeight)
	{
		const bool done = i < p1->lap;
		const bool current = (i == p1->lap) && !p1->finished;
		const ImVec2 rowMin(min.x, y + rowHeight * 0.06f);
		const ImVec2 rowMax(max.x, y + rowHeight * 0.94f);
		panel.draw->AddRectFilled(rowMin, rowMax, current ? kBlue : kFrame, u * 0.35f);
		const float textY = y + (rowHeight - u) * 0.5f;
		snprintf(text, sizeof(text), "LAP %d", i + 1);
		Text(panel, u, ImVec2(min.x + u * 0.5f, textY), current ? kYellow : kWhite, text);

		const int time = done ? p1->lapTimes[i] : current ? p1->currentLapTime : 0;
		FormatTime(text, sizeof(text), time);
		const bool isBest = done && (time == best);
		Text(panel, u, ImVec2(max.x - u * 4.6f, textY), isBest ? kGreen : (time > 0) ? kWhite : kMutedText, text, 1.0f);
		if (done && (best > 0))
		{
			if (isBest)
			{
				Text(panel, u * 0.8f, ImVec2(max.x - u * 0.5f, textY + u * 0.1f), kGreen, "BEST", 1.0f);
			}
			else
			{
				FormatDelta(text, sizeof(text), time - best);
				Text(panel, u * 0.8f, ImVec2(max.x - u * 0.5f, textY + u * 0.1f), kRed, text, 1.0f);
			}
		}
	}

	FormatTime(text, sizeof(text), state.raceTime);
	char total[48];
	snprintf(total, sizeof(total), "TOTAL  %s", text);
	Text(panel, u, ImVec2(centreX, max.y - u * 1.2f), kWhite, total, 0.5f);
}

void DrawIdlePage(const Panel &panel, const NativeSecondScreenState &state, ImVec2 min, ImVec2 max)
{
	const float u = panel.unit;
	const float centreX = (min.x + max.x) * 0.5f;
	const float centreY = (min.y + max.y) * 0.5f;
	Text(panel, u * 2.4f, ImVec2(centreX, centreY - u * 2.4f), kYellow, NATIVE_PRODUCT_NAME, 0.5f);
	Text(panel, u, ImVec2(centreX, centreY), kMutedText, (state.levelName[0] != '\0') ? state.levelName : "Second screen", 0.5f);
	Text(panel, u * 0.8f, ImVec2(centreX, centreY + u * 1.6f), kMutedText, "The map and race info show here during a race.", 0.5f);
}

// The whole panel in pos..pos+size. interactive: the page strip takes taps.
void DrawPanel(ImVec2 pos, ImVec2 size, bool interactive, int identifyNumber, float rounding)
{
	const NativeSecondScreenState &state = g_secondScreenState;
	if (!state.inRace)
	{
		s_pageOverride = -1;
		s_topSpeed = 1;
	}

	Panel panel;
	panel.draw = ImGui::GetWindowDrawList();
	panel.font = ImGui::GetFont();
	panel.unit = SDL_clamp(SDL_min(size.x, size.y * 0.85f) / 17.0f, 9.0f, 96.0f);
	panel.time = ImGui::GetTime();
	const float u = panel.unit;
	const ImVec2 max(pos.x + size.x, pos.y + size.y);

	panel.draw->AddRectFilled(pos, max, kNavy, rounding);
	panel.draw->AddRect(pos, max, kOrange, rounding, 0, SDL_max(u * 0.12f, 2.0f));

	const int page = CurrentPage(state);
	ImVec2 contentMin(pos.x + u * 0.6f, pos.y + u * 0.6f);
	const ImVec2 contentMax(max.x - u * 0.6f, max.y - u * 0.6f);
	if (page != kPageIdle)
	{
		// Page strip: tap a page to switch.
		const float stripHeight = u * 1.7f;
		const float tabWidth = (contentMax.x - contentMin.x) / (float)kPageCount;
		for (int i = 0; i < kPageCount; i++)
		{
			const ImVec2 tabMin(contentMin.x + tabWidth * i + u * 0.1f, contentMin.y);
			const ImVec2 tabMax(contentMin.x + tabWidth * (i + 1) - u * 0.1f, contentMin.y + stripHeight);
			const bool selected = i == page;
			const bool available = !(state.battle && (i == kPageTimes));
			if (interactive && available)
			{
				ImGui::SetCursorScreenPos(tabMin);
				ImGui::PushID(i);
				if (ImGui::InvisibleButton("##page", ImVec2(tabMax.x - tabMin.x, tabMax.y - tabMin.y)))
				{
					s_pageOverride = i;
				}
				ImGui::PopID();
			}
			panel.draw->AddRectFilled(tabMin, tabMax, selected ? kOrange : kDeepBlue, u * 0.4f);
			Text(panel, u * 0.9f, ImVec2((tabMin.x + tabMax.x) * 0.5f, tabMin.y + (stripHeight - u * 0.9f) * 0.5f),
			     selected ? kNavy : (available ? kMutedText : WithAlpha(kMutedText, 0.35f)), kPageNames[i], 0.5f);
		}
		contentMin.y += stripHeight + u * 0.6f;
	}

	panel.draw->PushClipRect(contentMin, contentMax, true);
	switch (page)
	{
	case kPageMap:
		DrawMapPage(panel, state, contentMin, contentMax);
		break;
	case kPageRace:
		DrawRacePage(panel, state, contentMin, contentMax);
		break;
	case kPageTimes:
		DrawTimesPage(panel, state, contentMin, contentMax);
		break;
	default:
		DrawIdlePage(panel, state, contentMin, contentMax);
		break;
	}
	panel.draw->PopClipRect();

	const int brightness = SDL_clamp(g_settings.secondScreen.brightness, NATIVE_SECOND_SCREEN_BRIGHTNESS_MIN, NATIVE_SECOND_SCREEN_BRIGHTNESS_MAX);
	if (brightness < 100)
	{
		panel.draw->AddRectFilled(pos, max, Rgb(0, 0, 0, (unsigned)((100 - brightness) * 255 / 100)), rounding);
	}

	if (identifyNumber > 0)
	{
		char number[8];
		snprintf(number, sizeof(number), "%d", identifyNumber);
		const float big = SDL_min(size.x, size.y) * 0.5f;
		panel.draw->AddRectFilled(pos, max, Rgb(0, 0, 0, 170), rounding);
		Text(panel, big, ImVec2((pos.x + max.x) * 0.5f, (pos.y + max.y) * 0.5f - big * 0.5f), kYellow, number, 0.5f);
	}
}

void DestroyMapTexture()
{
	if (s_mapTexture == nullptr)
	{
		return;
	}
	if (s_mapTexture->Status == ImTextureStatus_OK)
	{
		s_mapTexture->SetStatus(ImTextureStatus_WantDestroy);
		s_mapTexture->UnusedFrames = 1;
		ImGui_ImplOpenGL3_UpdateTexture(s_mapTexture);
	}
	IM_DELETE(s_mapTexture);
	s_mapTexture = nullptr;
}

} // namespace

void NativeSecondScreenUI_UpdateTextures()
{
	int width = 0;
	int height = 0;
	int version = 0;
	const unsigned char *pixels = NativeSecondScreen_GetMapPixels(&width, &height, &version);
	const int scale = g_settings.secondScreen.mapSmooth ? 1 : kMapPixelScale;
	if ((version == s_mapVersion) && (scale == s_mapScale))
	{
		return;
	}
	DestroyMapTexture();
	s_mapVersion = version;
	s_mapScale = scale;
	s_mapWidth = 0;
	s_mapHeight = 0;
	if ((pixels == nullptr) || (width <= 0) || (height <= 0))
	{
		return;
	}

	s_mapTexture = IM_NEW(ImTextureData)();
	s_mapTexture->Create(ImTextureFormat_RGBA32, width * scale, height * scale);
	s_mapTexture->UseColors = true;
	unsigned char *out = (unsigned char *)s_mapTexture->GetPixels();
	for (int y = 0; y < height * scale; y++)
	{
		const unsigned char *row = pixels + (size_t)(y / scale) * width * 4;
		for (int x = 0; x < width * scale; x++, out += 4)
		{
			memcpy(out, row + (x / scale) * 4, 4);
		}
	}
	s_mapTexture->SetStatus(ImTextureStatus_WantCreate);
	ImGui_ImplOpenGL3_UpdateTexture(s_mapTexture);
	s_mapWidth = width;
	s_mapHeight = height;
}

void NativeSecondScreenUI_DestroyTextures()
{
	DestroyMapTexture();
	s_mapVersion = -1;
}

void NativeSecondScreenUI_DrawInset()
{
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	const float height = floorf(viewport->WorkSize.y * kInsetHeight);
	const ImVec2 size(floorf(height * kInsetAspect), height);
	const float pad = floorf(viewport->WorkSize.y * 0.02f);
	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - size.x - pad, viewport->WorkPos.y + viewport->WorkSize.y - size.y - pad));
	ImGui::SetNextWindowSize(size);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
	                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
	                               ImGuiWindowFlags_NoBringToFrontOnFocus;
	if (ImGui::Begin("##secondScreenInset", nullptr, flags))
	{
		DrawPanel(ImGui::GetWindowPos(), size, true, 0, floorf(height * 0.04f));
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}

void NativeSecondScreenUI_DrawIdentify(int displayNumber)
{
	char number[8];
	snprintf(number, sizeof(number), "%d", displayNumber);
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	const float big = SDL_min(viewport->WorkSize.x, viewport->WorkSize.y) * 0.4f;
	ImDrawList *draw = ImGui::GetForegroundDrawList();
	const ImVec2 size = ImGui::GetFont()->CalcTextSizeA(big, FLT_MAX, 0.0f, number);
	const ImVec2 centre = viewport->GetWorkCenter();
	draw->AddRectFilled(ImVec2(centre.x - big * 0.5f, centre.y - big * 0.55f), ImVec2(centre.x + big * 0.5f, centre.y + big * 0.55f), Rgb(0, 0, 0, 170),
	                    big * 0.1f);
	draw->AddText(ImGui::GetFont(), big, ImVec2(centre.x - size.x * 0.5f, centre.y - size.y * 0.5f), kYellow, number);
}

extern "C" void NativeSecondScreenUI_RenderWindow(int width, int height, int pixelWidth, int pixelHeight, int identifyNumber)
{
	ImGuiContext *mainContext = ImGui::GetCurrentContext();
	if (mainContext == nullptr)
	{
		return;
	}

	if (s_context == nullptr)
	{
		// Shares the menu's font atlas (and so its GL font texture, via the shared GL context).
		s_context = ImGui::CreateContext(ImGui::GetIO().Fonts);
		ImGui::SetCurrentContext(s_context);
		ImGuiIO &io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.LogFilename = nullptr;
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		s_backendReady = ImGui_ImplOpenGL3_Init("#version 150");
	}
	else
	{
		ImGui::SetCurrentContext(s_context);
	}

	if (s_backendReady)
	{
		ImGuiIO &io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)width, (float)height);
		io.DisplayFramebufferScale = ImVec2((float)pixelWidth / (float)width, (float)pixelHeight / (float)height);
		const Uint64 now = SDL_GetTicks();
		static Uint64 s_lastFrame = 0;
		io.DeltaTime = (s_lastFrame != 0) ? SDL_max((float)(now - s_lastFrame) / 1000.0f, 1e-3f) : (1.0f / 60.0f);
		s_lastFrame = now;

		ImGui_ImplOpenGL3_NewFrame();
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(io.DisplaySize);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
		                               ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus;
		if (ImGui::Begin("##secondScreen", nullptr, flags))
		{
			DrawPanel(ImVec2(0.0f, 0.0f), io.DisplaySize, true, identifyNumber, 0.0f);
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	}

	ImGui::SetCurrentContext(mainContext);
}

extern "C" void NativeSecondScreenUI_ProcessEvent(const SDL_Event *event)
{
	if (s_context == nullptr)
	{
		return;
	}
	ImGuiContext *mainContext = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(s_context);
	ImGuiIO &io = ImGui::GetIO();
	switch (event->type)
	{
	case SDL_EVENT_MOUSE_MOTION:
		io.AddMousePosEvent(event->motion.x, event->motion.y);
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		if (event->button.button == SDL_BUTTON_LEFT)
		{
			io.AddMousePosEvent(event->button.x, event->button.y);
			io.AddMouseButtonEvent(0, event->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
			// A lifted finger leaves nothing hovered.
			if ((event->type == SDL_EVENT_MOUSE_BUTTON_UP) && (event->button.which == SDL_TOUCH_MOUSEID))
			{
				io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
			}
		}
		break;
	default:
		break;
	}
	ImGui::SetCurrentContext(mainContext);
}

extern "C" void NativeSecondScreenUI_Shutdown(void)
{
	if (s_context == nullptr)
	{
		return;
	}
	ImGuiContext *mainContext = ImGui::GetCurrentContext();
	ImGui::SetCurrentContext(s_context);
	if (s_backendReady)
	{
		ImGui_ImplOpenGL3_Shutdown();
	}
	ImGui::DestroyContext(s_context);
	s_context = nullptr;
	s_backendReady = false;
	ImGui::SetCurrentContext(mainContext);
}
