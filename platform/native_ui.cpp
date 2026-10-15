// PowerSlide in-game settings menu built on Dear ImGui (externals/imgui).
// F1 (or Esc while open) toggles one tabbed settings window, styled after
// the game's own orange/yellow-on-blue menus. Overlays such as the FPS counter
// draw whether or not the menu is open. The game keeps running underneath; the
// menu holds back only the device that opened it (the keyboard, or one controller).

#include <platform/native_ui.h>
#include <platform/native_branding.h>
#include <platform/native_input_config.h>
#include <platform/native_product.h>
#include <platform/native_settings.h>
#include <platform/native_tweaks.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "imgui_internal.h"
#include "native_ui_font.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef CTR_NATIVE_VERSION
#define CTR_NATIVE_VERSION "0.0.0-dev"
#endif

#ifndef CTR_NATIVE_BUILD_ID
#define CTR_NATIVE_BUILD_ID "unknown"
#endif

namespace
{

// Font size for a kReferenceHeight-tall window. The UI grows with the window
// height at half the rate (1.5x at twice the height), so a fullscreen menu stays
// a comfortable fraction of the screen; it never shrinks below kMinHeightScale.
// Display DPI is not applied on top: the window's own height already reflects it.
// (The touch layout below is the exception and sizes by display density.)
constexpr float kBaseFontSize = 16.0f;
constexpr float kReferenceHeight = 720.0f;
constexpr float kHeightScaleRate = 0.5f;
constexpr float kMinHeightScale = 0.75f;
// Logo height in the menu header, in font sizes.
constexpr float kLogoFontScale = 2.0f;

// Touch layout (Android, CTR_NATIVE_TOUCH_UI): the menu fills the screen height with
// finger-sized rows, its pages scroll by dragging, sliders become -/+ steppers and
// help text opens from a "?" button instead of a hover tooltip.
#if defined(CTR_NATIVE_TOUCH_UI)
constexpr bool kTouchUi = true;
#else
constexpr bool kTouchUi = false;
#endif
// Text size per display density unit (about 18 dp), with room for at least kTouchMinLines lines.
constexpr float kTouchFontScale = 1.125f;
constexpr float kTouchMinLines = 16.0f;
// Widest the menu gets, and the width of help popups, in font sizes.
constexpr float kTouchMaxWidthFonts = 44.0f;
constexpr float kTouchHelpWidthFonts = 28.0f;
// A fling slows by this factor per second and stops below kTouchFlingStop font sizes per second.
constexpr float kTouchFlingDecay = 4.0f;
constexpr float kTouchFlingStop = 2.0f;
// A release this long after the finger last moved doesn't fling.
constexpr double kTouchFlingMaxPause = 0.1;

// Window size presets are these heights at the picture's aspect ratio.
constexpr int kWindowHeights[] = {480, 720, 900, 1080, 1440, 2160};

constexpr const char *kDisplayModeNames[NATIVE_DISPLAY_COUNT] = {"Windowed", "Fullscreen"};
constexpr const char *kAspectModeNames[NATIVE_ASPECT_COUNT] = {"Original (4:3)", "Widescreen (16:9)", "Ultrawide (21:9)", "Custom",
                                                               "Stretch to window"};
constexpr const char *kScaleFilterNames[NATIVE_SCALE_FILTER_COUNT] = {"Sharp (nearest)", "Smooth (bilinear)"};
constexpr const char *kHudPositionNames[NATIVE_HUD_POSITION_COUNT] = {"Screen edges", "Centre (4:3)"};

// CTR palette, taken from the retail menus: orange and yellow on deep blue.
constexpr ImU32 Rgb(unsigned r, unsigned g, unsigned b)
{
	return IM_COL32(r, g, b, 255);
}

ImVec4 Color(ImU32 rgb, float alpha = 1.0f)
{
	ImVec4 c = ImGui::ColorConvertU32ToFloat4(rgb);
	c.w = alpha;
	return c;
}

constexpr ImU32 kNavy = Rgb(0x12, 0x18, 0x3A);
constexpr ImU32 kDeepBlue = Rgb(0x1B, 0x2A, 0x78);
constexpr ImU32 kBlue = Rgb(0x23, 0x40, 0xA8);
constexpr ImU32 kBrightBlue = Rgb(0x30, 0x55, 0xD0);
constexpr ImU32 kFrame = Rgb(0x1E, 0x2A, 0x63);
constexpr ImU32 kFrameHover = Rgb(0x29, 0x39, 0x8A);
constexpr ImU32 kOrange = Rgb(0xE8, 0x73, 0x1A);
constexpr ImU32 kOrangeLight = Rgb(0xF5, 0x9A, 0x2A);
constexpr ImU32 kOrangeDark = Rgb(0xC9, 0x55, 0x0E);
constexpr ImU32 kYellow = Rgb(0xFF, 0xD2, 0x1F);
constexpr ImU32 kMutedText = Rgb(0x9A, 0xA4, 0xD8);

SDL_Window *s_window = nullptr;
bool s_initialized = false;
bool s_menuOpen = false;
// Controller that opened the menu and navigates it; 0 when the keyboard and mouse drive it.
SDL_JoystickID s_menuController = 0;
// Set by main.c for packaged builds, which have first-run disc setup.
bool s_discChangeEnabled = false;
// Input tab's player to select when it next draws (the player whose controller opened the menu), or -1.
int s_selectPlayerTab = -1;
float s_styleScale = 0.0f;
// Video tab's custom window size fields, filled from the window when first shown.
bool s_customWindowSize = false;
int s_customWindowWidth = 0;
int s_customWindowHeight = 0;
// Menu header logo, resampled from the embedded image to the size it is drawn at.
ImTextureData *s_logoTexture = nullptr;

void ApplyColors(ImGuiStyle &style)
{
	ImVec4 *c = style.Colors;
	c[ImGuiCol_Text] = Color(Rgb(0xFF, 0xFF, 0xFF));
	c[ImGuiCol_TextDisabled] = Color(kMutedText);
	c[ImGuiCol_WindowBg] = Color(kNavy, 0.96f);
	c[ImGuiCol_ChildBg] = Color(kNavy, 0.0f);
	c[ImGuiCol_PopupBg] = Color(kNavy, 0.98f);
	c[ImGuiCol_Border] = Color(kOrange, 0.85f);
	c[ImGuiCol_BorderShadow] = Color(0, 0.0f);
	c[ImGuiCol_FrameBg] = Color(kFrame);
	c[ImGuiCol_FrameBgHovered] = Color(kFrameHover);
	c[ImGuiCol_FrameBgActive] = Color(kBlue);
	c[ImGuiCol_TitleBg] = Color(kDeepBlue);
	c[ImGuiCol_TitleBgActive] = Color(kBlue);
	c[ImGuiCol_TitleBgCollapsed] = Color(kDeepBlue, 0.8f);
	c[ImGuiCol_ScrollbarBg] = Color(kNavy, 0.6f);
	c[ImGuiCol_ScrollbarGrab] = Color(kBlue);
	c[ImGuiCol_ScrollbarGrabHovered] = Color(kBrightBlue);
	c[ImGuiCol_ScrollbarGrabActive] = Color(kOrange);
	c[ImGuiCol_CheckMark] = Color(kYellow);
	c[ImGuiCol_SliderGrab] = Color(kOrangeLight);
	c[ImGuiCol_SliderGrabActive] = Color(kYellow);
	c[ImGuiCol_Button] = Color(kOrange);
	c[ImGuiCol_ButtonHovered] = Color(kOrangeLight);
	c[ImGuiCol_ButtonActive] = Color(kOrangeDark);
	c[ImGuiCol_Header] = Color(kBlue);
	c[ImGuiCol_HeaderHovered] = Color(kBrightBlue);
	c[ImGuiCol_HeaderActive] = Color(kOrange);
	c[ImGuiCol_Separator] = Color(kOrange, 0.55f);
	c[ImGuiCol_SeparatorHovered] = Color(kOrangeLight);
	c[ImGuiCol_SeparatorActive] = Color(kYellow);
	c[ImGuiCol_ResizeGrip] = Color(kOrange, 0.3f);
	c[ImGuiCol_ResizeGripHovered] = Color(kOrangeLight, 0.7f);
	c[ImGuiCol_ResizeGripActive] = Color(kYellow);
	c[ImGuiCol_Tab] = Color(kDeepBlue);
	c[ImGuiCol_TabHovered] = Color(kOrangeLight);
	c[ImGuiCol_TabSelected] = Color(kOrange);
	c[ImGuiCol_TabSelectedOverline] = Color(kYellow);
	c[ImGuiCol_TabDimmed] = Color(kDeepBlue);
	c[ImGuiCol_TabDimmedSelected] = Color(kOrangeDark);
	c[ImGuiCol_TextSelectedBg] = Color(kOrange, 0.45f);
	c[ImGuiCol_NavCursor] = Color(kYellow);
	c[ImGuiCol_ModalWindowDimBg] = Color(kNavy, 0.6f);
}

void ApplyStyle(float scale)
{
	ImGuiStyle &style = ImGui::GetStyle();
	style = ImGuiStyle();
	ApplyColors(style);

	style.WindowRounding = 12.0f;
	style.ChildRounding = 8.0f;
	style.FrameRounding = 8.0f;
	style.PopupRounding = 8.0f;
	style.GrabRounding = 8.0f;
	style.TabRounding = 8.0f;
	style.ScrollbarRounding = 8.0f;
	style.WindowBorderSize = 2.0f;
	style.PopupBorderSize = 2.0f;
	style.FrameBorderSize = 0.0f;
	style.TabBorderSize = 0.0f;
	style.WindowPadding = ImVec2(18.0f, 14.0f);
	style.FramePadding = ImVec2(10.0f, 5.0f);
	style.ItemSpacing = ImVec2(10.0f, 8.0f);
	style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
	style.SeparatorTextBorderSize = 2.0f;
	style.SeparatorTextPadding = ImVec2(0.0f, 4.0f);
	if (kTouchUi)
	{
		// Rows about 44 dp tall; the thin scrollbar only shows where the page is.
		style.FramePadding = ImVec2(12.0f, 12.0f);
		style.ItemSpacing = ImVec2(12.0f, 10.0f);
		style.ItemInnerSpacing = ImVec2(10.0f, 8.0f);
		style.TouchExtraPadding = ImVec2(2.0f, 2.0f);
		style.GrabMinSize = 20.0f;
		style.ScrollbarSize = 6.0f;
		style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
	}
	style.ScaleAllSizes(scale);

	style.FontSizeBase = kBaseFontSize;
	style._NextFrameFontSizeBase = kBaseFontSize;
	style.FontScaleDpi = scale;
	s_styleScale = scale;
}

void LoadFont()
{
	ImFontConfig config;
	config.FontDataOwnedByAtlas = false;
	snprintf(config.Name, sizeof(config.Name), "Fredoka SemiBold");
	ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char *>(kNativeUiFontData), (int)sizeof(kNativeUiFontData), kBaseFontSize, &config);
}

void DestroyLogoTexture()
{
	if (s_logoTexture == nullptr)
	{
		return;
	}
	if (s_logoTexture->Status == ImTextureStatus_OK)
	{
		s_logoTexture->SetStatus(ImTextureStatus_WantDestroy);
		s_logoTexture->UnusedFrames = 1;
		ImGui_ImplOpenGL3_UpdateTexture(s_logoTexture);
	}
	IM_DELETE(s_logoTexture);
	s_logoTexture = nullptr;
}

// Box-filters the logo down to the height it is drawn at, so it stays smooth at every
// menu size (the GL backend samples bilinearly, without mipmaps). Runs between frames.
void UpdateLogoTexture()
{
	int sourceWidth = 0;
	int sourceHeight = 0;
	const unsigned char *source = NativeBranding_GetLogo(&sourceWidth, &sourceHeight);
	const int height = SDL_clamp((int)(kBaseFontSize * s_styleScale * kLogoFontScale + 0.5f), 1, sourceHeight);
	if ((s_logoTexture != nullptr) && (s_logoTexture->Height == height))
	{
		return;
	}
	DestroyLogoTexture();

	const int width = SDL_max((sourceWidth * height + sourceHeight / 2) / sourceHeight, 1);
	s_logoTexture = IM_NEW(ImTextureData)();
	s_logoTexture->Create(ImTextureFormat_RGBA32, width, height);
	s_logoTexture->UseColors = true;

	unsigned char *out = (unsigned char *)s_logoTexture->GetPixels();
	for (int y = 0; y < height; y++)
	{
		const int y0 = y * sourceHeight / height;
		const int y1 = SDL_max((y + 1) * sourceHeight / height, y0 + 1);
		for (int x = 0; x < width; x++, out += 4)
		{
			const int x0 = x * sourceWidth / width;
			const int x1 = SDL_max((x + 1) * sourceWidth / width, x0 + 1);
			// Average with premultiplied alpha, so transparent pixels don't darken the edges.
			unsigned int sum[4] = {0, 0, 0, 0};
			for (int sy = y0; sy < y1; sy++)
			{
				for (int sx = x0; sx < x1; sx++)
				{
					const unsigned char *in = &source[(sy * sourceWidth + sx) * 4];
					sum[0] += in[0] * in[3];
					sum[1] += in[1] * in[3];
					sum[2] += in[2] * in[3];
					sum[3] += in[3];
				}
			}
			const unsigned int count = (unsigned int)((y1 - y0) * (x1 - x0));
			for (int c = 0; c < 3; c++)
			{
				out[c] = (unsigned char)((sum[3] != 0) ? (sum[c] + sum[3] / 2) / sum[3] : 0);
			}
			out[3] = (unsigned char)((sum[3] + count / 2) / count);
		}
	}

	s_logoTexture->SetStatus(ImTextureStatus_WantCreate);
	ImGui_ImplOpenGL3_UpdateTexture(s_logoTexture);
}

void UpdateStyleScale()
{
	float scale = 1.0f;

	int width = 0;
	int height = 0;
	SDL_GetWindowSize(s_window, &width, &height);
	if (height > 0)
	{
		// Follow the window height in eighth steps so a window drag doesn't rebuild the
		// style every pixel; the floor keeps small windows readable.
		const float heightScale = 1.0f + (height / kReferenceHeight - 1.0f) * kHeightScaleRate;
		scale = SDL_max(SDL_floorf(heightScale * 8.0f) / 8.0f, kMinHeightScale);

		// A phone's screen is a few inches tall however many pixels it has, so size
		// the touch layout by display density instead.
		const float density = kTouchUi ? SDL_GetWindowDisplayScale(s_window) : 0.0f;
		if (density > 0.0f)
		{
			const float touchScale = SDL_min(density * kTouchFontScale, height / (kBaseFontSize * kTouchMinLines));
			scale = SDL_max(SDL_floorf(touchScale * 8.0f) / 8.0f, kMinHeightScale);
		}
	}

	if (scale != s_styleScale)
	{
		ApplyStyle(scale);
	}
}

void CancelCapture();

// ImGui navigates with the menu's controller only, so other players' controllers stay in the game.
void SetMenuController(SDL_JoystickID controller)
{
	SDL_Gamepad *gamepad = (controller != 0) ? SDL_GetGamepadFromID(controller) : nullptr;
	s_menuController = (gamepad != nullptr) ? controller : 0;
	ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, &gamepad, (gamepad != nullptr) ? 1 : 0);
}

void SetMenuOpen(bool open, SDL_JoystickID controller)
{
	if (open)
	{
		SetMenuController(controller);
	}
	if (s_menuOpen == open)
	{
		return;
	}
	s_menuOpen = open;
	CancelCapture();
	if (!open)
	{
		SetMenuController(0);
	}
	Platform_UpdateCursorVisibility();
}

void CommitVideoSettings()
{
	Platform_ApplyVideoSettings();
	NativeSettings_Save();
}

bool CheckboxInt(const char *label, int *value)
{
	bool checked = *value != 0;
	if (!ImGui::Checkbox(label, &checked))
	{
		return false;
	}
	*value = checked ? 1 : 0;
	return true;
}

// Help for the item just drawn: a hover tooltip, or on touch screens (which can't hover)
// a "?" button that opens the text.
void HelpTooltip(const char *text)
{
	if (!kTouchUi)
	{
		ImGui::SetItemTooltip("%s", text);
		return;
	}

	ImGui::PushID(text);
	// Help stays readable next to options that are locked.
	const bool disabled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
	if (disabled)
	{
		ImGui::BeginDisabledOverrideReenable();
	}
	ImGui::SameLine();
	ImGui::PushStyleColor(ImGuiCol_Button, Color(kFrame));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(kFrameHover));
	ImGui::PushStyleColor(ImGuiCol_Text, Color(kYellow));
	if (ImGui::Button("?", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
	{
		ImGui::OpenPopup("##help");
	}
	ImGui::PopStyleColor(3);
	if (disabled)
	{
		ImGui::EndDisabledOverrideReenable();
	}
	if (ImGui::BeginPopup("##help"))
	{
		ImGui::PushTextWrapPos(ImGui::GetFontSize() * kTouchHelpWidthFonts);
		ImGui::TextUnformatted(text);
		ImGui::PopTextWrapPos();
		ImGui::EndPopup();
	}
	ImGui::PopID();
}

void SectionHeading(const char *text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, Color(kYellow));
	ImGui::SeparatorText(text);
	ImGui::PopStyleColor();
}

// Drag-to-scroll with fling for the current window on touch screens (ImGui itself only
// scrolls with a wheel or the scrollbar). Once a press turns into a vertical drag, the
// item under the finger is let go, so the swipe doesn't also click it. Call just before
// ending the window, child or combo.
struct TouchScroll
{
	ImGuiID window = 0;
	bool dragging = false;
	bool flinging = false;
	float velocity = 0.0f;
	double lastMoveTime = 0.0;
};

TouchScroll s_touchScroll;

void TouchScrollWindow()
{
	if (!kTouchUi)
	{
		return;
	}

	const ImGuiIO &io = ImGui::GetIO();
	const ImGuiID window = ImGui::GetCurrentWindow()->ID;
	TouchScroll &scroll = s_touchScroll;

	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && (io.MouseSource == ImGuiMouseSource_TouchScreen) &&
	    ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
	{
		scroll = TouchScroll();
		scroll.window = window;
	}
	if (scroll.window != window)
	{
		return;
	}

	if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
	{
		const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
		if (!scroll.dragging && (drag.y != 0.0f) && (fabsf(drag.y) > fabsf(drag.x)))
		{
			scroll.dragging = true;
			ImGui::ClearActiveID();
		}
		if (scroll.dragging && (io.MouseDelta.y != 0.0f))
		{
			ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
			if (io.DeltaTime > 0.0f)
			{
				// Smoothed, so one uneven frame doesn't decide the fling.
				scroll.velocity = scroll.velocity * 0.5f - io.MouseDelta.y / io.DeltaTime * 0.5f;
			}
			scroll.lastMoveTime = ImGui::GetTime();
		}
		return;
	}

	if (scroll.dragging)
	{
		scroll.dragging = false;
		scroll.flinging = (ImGui::GetTime() - scroll.lastMoveTime) < kTouchFlingMaxPause;
	}
	if (!scroll.flinging)
	{
		scroll.window = 0;
		return;
	}

	const float y = ImGui::GetScrollY() + scroll.velocity * io.DeltaTime;
	ImGui::SetScrollY(y);
	scroll.velocity *= expf(-kTouchFlingDecay * io.DeltaTime);
	if ((fabsf(scroll.velocity) < ImGui::GetFontSize() * kTouchFlingStop) || (y <= 0.0f) || (y >= ImGui::GetScrollMaxY()))
	{
		scroll.window = 0;
	}
}

// Touch screen stand-in for a slider: tap or hold - and + to step the value, so a swipe
// that scrolls past it can't change it. Steps land on multiples of step. Returns whether
// the value changed (it applies and saves at once).
bool StepperInt(const char *label, int *value, int min, int max, int step, const char *format)
{
	const int old = *value;
	const ImGuiStyle &style = ImGui::GetStyle();
	const float button = ImGui::GetFrameHeight();
	float valueWidth = ImGui::CalcItemWidth() - 2.0f * (button + style.ItemInnerSpacing.x);

	ImGui::PushID(label);
	ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
	if (ImGui::Button("-", ImVec2(button, 0.0f)))
	{
		*value = SDL_clamp((*value - 1) / step * step, min, max);
	}
	ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);

	char text[64];
	snprintf(text, sizeof(text), format, *value);
	valueWidth = SDL_max(valueWidth, ImGui::CalcTextSize(text).x + style.FramePadding.x * 2.0f);
	ImGui::PushStyleColor(ImGuiCol_Button, Color(kFrame));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Color(kFrame));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, Color(kFrame));
	ImGui::Button(text, ImVec2(valueWidth, 0.0f));
	ImGui::PopStyleColor(3);

	ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
	if (ImGui::Button("+", ImVec2(button, 0.0f)))
	{
		*value = SDL_clamp((*value / step + 1) * step, min, max);
	}
	ImGui::PopItemFlag();
	ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(label);
	ImGui::PopID();
	return *value != old;
}

// One entry of a dropdown, a full row tall on touch screens.
bool ComboItem(const char *label, bool selected)
{
	return ImGui::Selectable(label, selected, ImGuiSelectableFlags_None, kTouchUi ? ImVec2(0.0f, ImGui::GetFrameHeight()) : ImVec2(0.0f, 0.0f));
}

bool ComboInt(const char *label, int *value, const char *const names[], int count)
{
	if (!kTouchUi)
	{
		return ImGui::Combo(label, value, names, count);
	}

	bool changed = false;
	if (ImGui::BeginCombo(label, ((*value >= 0) && (*value < count)) ? names[*value] : ""))
	{
		for (int i = 0; i < count; i++)
		{
			ImGui::PushID(i);
			if (ComboItem(names[i], i == *value))
			{
				*value = i;
				changed = true;
			}
			if ((i == *value) && ImGui::IsWindowAppearing())
			{
				ImGui::SetItemDefaultFocus();
			}
			ImGui::PopID();
		}
		TouchScrollWindow();
		ImGui::EndCombo();
	}
	return changed;
}

// Refresh rate of the display the window is on, rounded (59.94 -> 60); 0 if unknown.
int DisplayRefreshRate()
{
	const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_window));
	if ((mode == nullptr) || (mode->refresh_rate <= 0.0f))
	{
		return 0;
	}
	return (int)(mode->refresh_rate + 0.5f);
}

// Picture aspect the window size presets follow: the chosen ratio, or the
// display's own shape when stretching.
void PresetAspect(int *width, int *height)
{
	if (NativeSettings_AspectRatio(width, height))
	{
		return;
	}

	const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_window));
	if ((mode != nullptr) && (mode->w > 0) && (mode->h > 0))
	{
		*width = mode->w;
		*height = mode->h;
		return;
	}
	*width = 16;
	*height = 9;
}

// Sliders and number fields apply live while edited and are saved once released.
bool EditCommitted()
{
	return ImGui::IsItemDeactivatedAfterEdit();
}

bool DrawWindowSize(NativeVideoSettings &video, float comboWidth)
{
	const SDL_DisplayMode *display = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_window));

	if (video.displayMode != NATIVE_DISPLAY_WINDOWED)
	{
		if (display != nullptr)
		{
			ImGui::TextDisabled("Fullscreen uses your desktop resolution (%d x %d).", display->w, display->h);
		}
		return false;
	}

	bool changed = false;
	int currentWidth = 0;
	int currentHeight = 0;
	SDL_GetWindowSize(s_window, &currentWidth, &currentHeight);

	int aspectW = 4;
	int aspectH = 3;
	PresetAspect(&aspectW, &aspectH);

	// Only sizes that fit on the display the window is on.
	const int maxWidth = (display != nullptr) ? display->w : 16384;
	const int maxHeight = (display != nullptr) ? display->h : 16384;

	char preview[32];
	snprintf(preview, sizeof(preview), s_customWindowSize ? "Custom" : "%d x %d", currentWidth, currentHeight);

	ImGui::SetNextItemWidth(comboWidth);
	if (ImGui::BeginCombo("Window size", preview))
	{
		for (int presetHeight : kWindowHeights)
		{
			// Even widths keep the picture centred on a whole pixel.
			const int presetWidth = ((presetHeight * aspectW / aspectH) + 1) & ~1;
			if ((presetWidth > maxWidth) || (presetHeight > maxHeight))
			{
				continue;
			}

			char label[32];
			snprintf(label, sizeof(label), "%d x %d", presetWidth, presetHeight);
			const bool selected = !s_customWindowSize && (presetWidth == currentWidth) && (presetHeight == currentHeight);
			if (ComboItem(label, selected))
			{
				s_customWindowSize = false;
				video.windowWidth = presetWidth;
				video.windowHeight = presetHeight;
				changed = true;
			}
		}

		if (ComboItem("Custom...", s_customWindowSize))
		{
			s_customWindowSize = true;
			s_customWindowWidth = currentWidth;
			s_customWindowHeight = currentHeight;
		}
		TouchScrollWindow();
		ImGui::EndCombo();
	}
	HelpTooltip("Sizes follow the aspect ratio below, so the picture fills the window without black bars. "
	            "You can also drag the window edges to any size.");

	if (s_customWindowSize)
	{
		const float fieldWidth = ImGui::GetFontSize() * 5.0f;
		ImGui::Indent();
		ImGui::SetNextItemWidth(fieldWidth);
		ImGui::InputInt("##windowWidth", &s_customWindowWidth, 0, 0);
		ImGui::SameLine();
		ImGui::TextUnformatted("x");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(fieldWidth);
		ImGui::InputInt("##windowHeight", &s_customWindowHeight, 0, 0);
		ImGui::SameLine();
		if (ImGui::Button("Apply"))
		{
			video.windowWidth = SDL_clamp(s_customWindowWidth, 320, maxWidth);
			video.windowHeight = SDL_clamp(s_customWindowHeight, 240, maxHeight);
			s_customWindowWidth = video.windowWidth;
			s_customWindowHeight = video.windowHeight;
			changed = true;
		}
		ImGui::Unindent();
	}
	return changed;
}

bool DrawFrameRate(NativeVideoSettings &video, float comboWidth)
{
	bool changed = false;

	ImGui::BeginDisabled(video.frameRateMatchDisplay != 0);
	int rate = video.frameRateMatchDisplay ? Platform_GetTargetFrameRate() : video.frameRate;
	const char *format = rate == NATIVE_FRAME_RATE_ORIGINAL ? "%d fps (original)" : "%d fps";
	ImGui::SetNextItemWidth(comboWidth);
	if (kTouchUi)
	{
		if (StepperInt("Frame rate", &rate, NATIVE_FRAME_RATE_MIN, NATIVE_FRAME_RATE_MAX, 10, format))
		{
			video.frameRate = rate;
			changed = true;
		}
	}
	else
	{
		if (ImGui::SliderInt("Frame rate", &rate, NATIVE_FRAME_RATE_MIN, NATIVE_FRAME_RATE_MAX, format, ImGuiSliderFlags_AlwaysClamp))
		{
			video.frameRate = rate;
		}
		changed |= EditCommitted();
	}
	ImGui::EndDisabled();
	HelpTooltip(kTouchUi ? "The game always runs at 30 updates per second. Above 30, extra frames are drawn in between by blending the "
	                       "camera, karts and objects; gameplay, physics, ghosts and replays are unchanged. Below 30, some frames are "
	                       "not shown. Menus and loading screens stay at 30."
	                     : "The game always runs at 30 updates per second. Above 30, extra frames are drawn in between by blending the "
	                       "camera, karts and objects; gameplay, physics, ghosts and replays are unchanged. Below 30, some frames are "
	                       "not shown. Menus and loading screens stay at 30. Ctrl+click to type a value.");

	const int display = DisplayRefreshRate();
	char label[48];
	snprintf(label, sizeof(label), display > 0 ? "Match display (%d Hz)" : "Match display", display);
	ImGui::Indent();
	changed |= CheckboxInt(label, &video.frameRateMatchDisplay);
	ImGui::Unindent();
	HelpTooltip("Follow the refresh rate of the display the window is on, instead of the slider.");
	return changed;
}

bool DrawInternalResolution(NativeVideoSettings &video, float comboWidth)
{
	bool changed = false;

	ImGui::BeginDisabled(video.renderScaleCustom != 0);
	const char *format = video.renderScale <= 1 ? "1x (original)" : "%dx";
	ImGui::SetNextItemWidth(comboWidth);
	if (kTouchUi)
	{
		changed |= StepperInt("Internal resolution", &video.renderScale, 1, NATIVE_RENDER_SCALE_MAX, 1, format);
	}
	else
	{
		ImGui::SliderInt("Internal resolution", &video.renderScale, 1, NATIVE_RENDER_SCALE_MAX, format, ImGuiSliderFlags_AlwaysClamp);
		changed |= EditCommitted();
	}
	ImGui::EndDisabled();
	HelpTooltip("Renders the game at a multiple of the original resolution for sharper edges. Above 1x the final image "
	            "also skips the PS1's 15-bit colour reduction. Screen effects (blur, heat haze) stay at 1x.");

	ImGui::Indent();
	changed |= CheckboxInt("Custom scale", &video.renderScaleCustom);
	HelpTooltip("Any multiplier from 1x to 16x, including fractions such as 2.5x.");
	if (video.renderScaleCustom)
	{
		ImGui::SameLine();
		float scale = (float)video.renderScalePercent / 100.0f;
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
		if (ImGui::InputFloat("##customScale", &scale, 0.25f, 1.0f, "%.2fx"))
		{
			const int percent = (int)(scale * 100.0f + 0.5f);
			video.renderScalePercent = SDL_clamp(percent, NATIVE_RENDER_SCALE_PERCENT_MIN, NATIVE_RENDER_SCALE_PERCENT_MAX);
			// The +/- buttons change the value without activating the field.
			changed |= !ImGui::IsItemActive();
		}
		changed |= EditCommitted();
	}

	// 512 x 216 is the in-race display; menus use other PS1 display sizes.
	const float multiplier = video.renderScaleCustom ? (float)video.renderScalePercent / 100.0f : (float)video.renderScale;
	const float widen = (multiplier > 1.0f) ? 1.0f / NativeSettings_WideSqueeze() : 1.0f;
	ImGui::TextDisabled("Races render at %d x %d.", (int)(512.0f * multiplier * widen + 0.5f), (int)(216.0f * multiplier + 0.5f));
	ImGui::Unindent();
	return changed;
}

bool DrawAspectRatio(NativeVideoSettings &video, float comboWidth)
{
	bool changed = false;

	ImGui::SetNextItemWidth(comboWidth);
	changed |= ComboInt("Aspect ratio", &video.aspectMode, kAspectModeNames, NATIVE_ASPECT_COUNT);
	HelpTooltip("Original keeps the 4:3 picture. Wider ratios show more of the world, with the HUD kept in its original "
	            "proportions. Stretch fills the window and distorts the image.");

	if (video.aspectMode != NATIVE_ASPECT_CUSTOM)
	{
		return changed;
	}

	const float fieldWidth = ImGui::GetFontSize() * 4.0f;
	ImGui::Indent();
	ImGui::SetNextItemWidth(fieldWidth);
	ImGui::InputInt("##aspectWidth", &video.aspectCustomWidth, 0, 0);
	changed |= EditCommitted();
	ImGui::SameLine();
	ImGui::TextUnformatted(":");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(fieldWidth);
	ImGui::InputInt("##aspectHeight", &video.aspectCustomHeight, 0, 0);
	changed |= EditCommitted();
	video.aspectCustomWidth = SDL_clamp(video.aspectCustomWidth, 1, NATIVE_ASPECT_CUSTOM_MAX_TERM);
	video.aspectCustomHeight = SDL_clamp(video.aspectCustomHeight, 1, NATIVE_ASPECT_CUSTOM_MAX_TERM);

	const SDL_DisplayMode *display = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(s_window));
	if (display != nullptr)
	{
		ImGui::SameLine();
		if (ImGui::Button("Use display"))
		{
			video.aspectCustomWidth = display->w;
			video.aspectCustomHeight = display->h;
			changed = true;
		}
		HelpTooltip("Match the shape of the display the window is on.");
	}

	int width = 0;
	int height = 0;
	NativeSettings_AspectRatio(&width, &height);
	if ((width != video.aspectCustomWidth) || (height != video.aspectCustomHeight))
	{
		ImGui::TextDisabled("Using %d:%d. Ratios range from 4:3 to 32:9.", width, height);
	}
	ImGui::Unindent();
	return changed;
}

// While PS1 mode is on, the options it resets are shown at their retail values and locked.
void Ps1ModeNotice()
{
	if (g_settings.global.ps1Mode)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, Color(kOrangeLight));
		ImGui::TextWrapped("PS1 mode is on: these options run at their original values. Turn it off in the Global tab to change them.");
		ImGui::PopStyleColor();
	}
}

void DrawVideoTab()
{
	NativeVideoSettings &video = g_settings.video;
	bool changed = false;
	const float comboWidth = ImGui::GetFontSize() * 12.0f;
	const bool ps1Mode = g_settings.global.ps1Mode != 0;

	Ps1ModeNotice();
	SectionHeading("Display");

	// Android is always fullscreen (the host enforces it), so it has no window options.
	if (!kTouchUi)
	{
		ImGui::SetNextItemWidth(comboWidth);
		changed |= ComboInt("Display mode", &video.displayMode, kDisplayModeNames, NATIVE_DISPLAY_COUNT);
		HelpTooltip("Fullscreen uses the desktop resolution (borderless). F11 or Alt+Enter also toggles it.");

		changed |= DrawWindowSize(video, comboWidth);
	}
	ImGui::BeginDisabled(ps1Mode);
	changed |= DrawFrameRate(video, comboWidth);

	SectionHeading("Picture");

	changed |= DrawInternalResolution(video, comboWidth);
	changed |= DrawAspectRatio(video, comboWidth);

	ImGui::BeginDisabled(!NativeSettings_WidescreenActive());
	ImGui::SetNextItemWidth(comboWidth);
	changed |= ComboInt("HUD position", &video.hudPosition, kHudPositionNames, NATIVE_HUD_POSITION_COUNT);
	ImGui::EndDisabled();
	HelpTooltip("Wide ratios only. Screen edges moves the race HUD out to the corners; centre keeps the original 4:3 layout. "
	            "Menus always stay centred.");

	ImGui::BeginDisabled(video.aspectMode == NATIVE_ASPECT_STRETCH);
	changed |= CheckboxInt("Integer scaling", &video.integerScaling);
	ImGui::EndDisabled();
	HelpTooltip("Scale by whole multiples of the original line count, so every line is the same height. May add wider borders.");

	ImGui::SetNextItemWidth(comboWidth);
	changed |= ComboInt("Scaling filter", &video.scaleFilter, kScaleFilterNames, NATIVE_SCALE_FILTER_COUNT);
	HelpTooltip("How the picture is enlarged to fit the window. Sharp keeps hard pixel edges; smooth blends them.");

	changed |= CheckboxInt("Texture filtering", &video.textureFiltering);
	HelpTooltip("Bilinear filtering of in-game textures. Not part of the original look.");
	ImGui::EndDisabled();

	SectionHeading("Overlay");
	changed |= CheckboxInt("Show FPS counter", &video.showFps);

	ImGui::Spacing();
	ImGui::BeginDisabled(ps1Mode);
	if (ImGui::Button("Reset video settings"))
	{
		const int keepWidth = video.windowWidth;
		const int keepHeight = video.windowHeight;
		NativeSettings defaults;
		NativeSettings_SetDefaults(&defaults);
		video = defaults.video;
		video.windowWidth = keepWidth;
		video.windowHeight = keepHeight;
		s_customWindowSize = false;
		changed = true;
	}
	ImGui::EndDisabled();
	HelpTooltip("Restores every video option except the window size.");

	if (changed)
	{
		CommitVideoSettings();
	}
}

//------------------------------------------------------------------------------------------------------------------------
// Input tab

constexpr const char *kPadButtonLabels[NATIVE_PAD_BUTTON_COUNT] = {
	"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2",
	"D-pad up", "D-pad down", "D-pad left", "D-pad right", "Start", "Select", "L3", "R3",
};
constexpr const char *kPlayerNames[NATIVE_INPUT_PLAYERS] = {"Player 1", "Player 2", "Player 3", "Player 4"};

// Columns of the bindings table that can be rebound.
enum BindColumn
{
	BIND_KEY_PRIMARY = 0,
	BIND_KEY_ALTERNATE = 1,
	BIND_GAMEPAD = 2,
};

constexpr Uint64 kCaptureIgnoreMs = 250;
constexpr Uint64 kCaptureTimeoutMs = 6000;
constexpr Sint16 kCaptureAxisThreshold = 20000;

struct BindingCapture
{
	bool active = false;
	int player = 0;
	int button = 0;
	int column = 0;
	Uint64 startedMs = 0;
};

BindingCapture s_capture;

void CancelCapture()
{
	s_capture.active = false;
}

void CommitInputSettings()
{
	Platform_ApplyInputSettings();
	NativeSettings_Save();
}

void StartCapture(int player, int button, int column)
{
	s_capture.active = true;
	s_capture.player = player;
	s_capture.button = button;
	s_capture.column = column;
	s_capture.startedMs = SDL_GetTicks();
}

// A key can only drive one PS1 button of one player; binding it elsewhere unbinds it there.
void AssignKey(int player, int button, int slot, int scancode)
{
	if (scancode != SDL_SCANCODE_UNKNOWN)
	{
		for (NativePlayerInputSettings &other : g_settings.input.players)
		{
			for (int b = 0; b < NATIVE_PAD_BUTTON_COUNT; b++)
			{
				for (int k = 0; k < NATIVE_KEY_BINDINGS; k++)
				{
					if (other.keyboard[b][k] == scancode)
					{
						other.keyboard[b][k] = SDL_SCANCODE_UNKNOWN;
					}
				}
			}
		}
	}
	g_settings.input.players[player].keyboard[button][slot] = scancode;
	s_capture.active = false;
	CommitInputSettings();
}

// A controller input drives one PS1 button per player (each player has its own controller).
void AssignPad(int player, int button, int binding)
{
	NativePlayerInputSettings &input = g_settings.input.players[player];
	if (binding != NATIVE_PAD_BIND_NONE)
	{
		for (int b = 0; b < NATIVE_PAD_BUTTON_COUNT; b++)
		{
			if (input.gamepad[b] == binding)
			{
				input.gamepad[b] = NATIVE_PAD_BIND_NONE;
			}
		}
	}
	input.gamepad[button] = binding;
	s_capture.active = false;
	CommitInputSettings();
}

// While a player has a controller, only that controller can bind that player's buttons.
bool CaptureAcceptsController(SDL_JoystickID which)
{
	const int controller = NativeInput_GetPlayerControllerId(s_capture.player);
	return (controller == 0) || ((SDL_JoystickID)controller == which);
}

// Returns true when the event was consumed by an active binding capture.
bool HandleCaptureEvent(const SDL_Event *event)
{
	if (!s_capture.active)
	{
		return false;
	}

	const Uint64 elapsed = SDL_GetTicks() - s_capture.startedMs;
	if (elapsed > kCaptureTimeoutMs)
	{
		s_capture.active = false;
		return false;
	}

	const bool keyColumn = s_capture.column != BIND_GAMEPAD;
	switch (event->type)
	{
	case SDL_EVENT_KEY_DOWN:
		if (event->key.repeat)
		{
			return true;
		}
		// NOTE: Android's Back button arrives as F1 (host_sdl.c).
		if ((event->key.scancode == SDL_SCANCODE_ESCAPE) || (kTouchUi && (event->key.scancode == SDL_SCANCODE_F1)))
		{
			s_capture.active = false;
		}
		else if ((event->key.scancode == SDL_SCANCODE_BACKSPACE) || (event->key.scancode == SDL_SCANCODE_DELETE))
		{
			if (keyColumn)
			{
				AssignKey(s_capture.player, s_capture.button, s_capture.column, SDL_SCANCODE_UNKNOWN);
			}
			else
			{
				AssignPad(s_capture.player, s_capture.button, NATIVE_PAD_BIND_NONE);
			}
		}
		else if (keyColumn && (event->key.scancode != SDL_SCANCODE_F1))
		{
			AssignKey(s_capture.player, s_capture.button, s_capture.column, event->key.scancode);
		}
		return true;

	case SDL_EVENT_KEY_UP:
		return true;

	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		// Ignore the press that started the capture (gamepad navigation's "accept").
		if (!keyColumn && (elapsed > kCaptureIgnoreMs) && (event->gbutton.button != SDL_GAMEPAD_BUTTON_GUIDE) &&
		    CaptureAcceptsController(event->gbutton.which))
		{
			AssignPad(s_capture.player, s_capture.button, event->gbutton.button);
		}
		return true;

	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		return true;

	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		if (!keyColumn && (elapsed > kCaptureIgnoreMs) && CaptureAcceptsController(event->gaxis.which) &&
		    ((event->gaxis.value > kCaptureAxisThreshold) || (event->gaxis.value < -kCaptureAxisThreshold)))
		{
			const int sign = (event->gaxis.value < 0) ? NATIVE_PAD_BIND_NEGATIVE : 0;
			AssignPad(s_capture.player, s_capture.button, NATIVE_PAD_BIND_AXIS | event->gaxis.axis | sign);
		}
		return true;

	default:
		return false;
	}
}

bool IsPlayStationPad(int type)
{
	return (type == SDL_GAMEPAD_TYPE_PS3) || (type == SDL_GAMEPAD_TYPE_PS4) || (type == SDL_GAMEPAD_TYPE_PS5);
}

// The controller type labels are drawn for: the player's own controller, else any connected one.
int LabelGamepadType(int player)
{
	int type = SDL_GAMEPAD_TYPE_STANDARD;
	if (NativeInput_GetPlayerController(player, nullptr, 0, &type))
	{
		return type;
	}
	for (int other = 0; other < NATIVE_INPUT_PLAYERS; other++)
	{
		if (NativeInput_GetPlayerController(other, nullptr, 0, &type))
		{
			return type;
		}
	}
	return SDL_GAMEPAD_TYPE_STANDARD;
}

const char *FaceButtonLabel(SDL_GamepadButton button, int type)
{
	switch (SDL_GetGamepadButtonLabelForType((SDL_GamepadType)type, button))
	{
	case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
	case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
	case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
	case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
	case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "Cross";
	case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
	case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "Square";
	case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "Triangle";
	default: return nullptr;
	}
}

void FormatPadBinding(int binding, int type, char *out, size_t outSize)
{
	const bool ps = IsPlayStationPad(type);
	if (binding == NATIVE_PAD_BIND_NONE)
	{
		snprintf(out, outSize, "Unbound");
		return;
	}

	if ((binding & NATIVE_PAD_BIND_AXIS) != 0)
	{
		const bool negative = (binding & NATIVE_PAD_BIND_NEGATIVE) != 0;
		switch (binding & ~(NATIVE_PAD_BIND_AXIS | NATIVE_PAD_BIND_NEGATIVE))
		{
		case SDL_GAMEPAD_AXIS_LEFTX: snprintf(out, outSize, "Left stick %s", negative ? "left" : "right"); return;
		case SDL_GAMEPAD_AXIS_LEFTY: snprintf(out, outSize, "Left stick %s", negative ? "up" : "down"); return;
		case SDL_GAMEPAD_AXIS_RIGHTX: snprintf(out, outSize, "Right stick %s", negative ? "left" : "right"); return;
		case SDL_GAMEPAD_AXIS_RIGHTY: snprintf(out, outSize, "Right stick %s", negative ? "up" : "down"); return;
		case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: snprintf(out, outSize, "%s", ps ? "L2" : "LT"); return;
		case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: snprintf(out, outSize, "%s", ps ? "R2" : "RT"); return;
		default: snprintf(out, outSize, "Axis %d", binding & 0xff); return;
		}
	}

	const SDL_GamepadButton button = (SDL_GamepadButton)binding;
	if (const char *face = FaceButtonLabel(button, type))
	{
		snprintf(out, outSize, "%s", face);
		return;
	}

	const char *label = nullptr;
	switch (button)
	{
	case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: label = ps ? "L1" : "LB"; break;
	case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: label = ps ? "R1" : "RB"; break;
	case SDL_GAMEPAD_BUTTON_LEFT_STICK: label = ps ? "L3" : "Left stick press"; break;
	case SDL_GAMEPAD_BUTTON_RIGHT_STICK: label = ps ? "R3" : "Right stick press"; break;
	case SDL_GAMEPAD_BUTTON_BACK: label = ps ? "Share / Create" : "Back / View"; break;
	case SDL_GAMEPAD_BUTTON_START: label = ps ? "Options" : "Start / Menu"; break;
	case SDL_GAMEPAD_BUTTON_GUIDE: label = "Home"; break;
	case SDL_GAMEPAD_BUTTON_DPAD_UP: label = "D-pad up"; break;
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN: label = "D-pad down"; break;
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT: label = "D-pad left"; break;
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: label = "D-pad right"; break;
	default: break;
	}
	if (label != nullptr)
	{
		snprintf(out, outSize, "%s", label);
		return;
	}

	const char *name = SDL_GetGamepadStringForButton(button);
	snprintf(out, outSize, "%s", name ? name : "?");
}

void FormatKey(int scancode, char *out, size_t outSize)
{
	const char *name = (scancode != SDL_SCANCODE_UNKNOWN) ? SDL_GetScancodeName((SDL_Scancode)scancode) : "";
	snprintf(out, outSize, "%s", (name[0] != '\0') ? name : "Unbound");
}

// One rebindable cell: shows the binding, or a prompt while capturing it.
void BindingCell(int player, int button, int column, const char *text, float width)
{
	const bool capturing = s_capture.active && (s_capture.player == player) && (s_capture.button == button) && (s_capture.column == column);
	char label[96];
	snprintf(label, sizeof(label), "%s##bind%d_%d", capturing ? (column == BIND_GAMEPAD ? "Press a button..." : "Press a key...") : text, button, column);

	if (capturing)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, Color(kYellow));
		ImGui::PushStyleColor(ImGuiCol_Text, Color(kNavy));
	}
	else
	{
		ImGui::PushStyleColor(ImGuiCol_Button, Color(kFrame));
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
	}
	if (ImGui::Button(label, ImVec2(width, 0.0f)))
	{
		StartCapture(player, button, column);
	}
	ImGui::PopStyleColor(2);
}

void DrawControllerSection(int player)
{
	NativePlayerInputSettings &input = g_settings.input.players[player];
	const float comboWidth = ImGui::GetFontSize() * 16.0f;

	SectionHeading("Controller");

	char current[96];
	const bool connected = NativeInput_GetPlayerController(player, current, sizeof(current), nullptr) != 0;
	if (!connected)
	{
		snprintf(current, sizeof(current), "None");
	}

	// Picking a controller another player has swaps the two players' controllers.
	ImGui::SetNextItemWidth(comboWidth);
	if (ImGui::BeginCombo("Controller", current))
	{
		for (int other = 0; other < NATIVE_INPUT_PLAYERS; other++)
		{
			char name[96];
			if (!NativeInput_GetPlayerController(other, name, sizeof(name), nullptr))
			{
				continue;
			}
			char label[128];
			if (other == player)
			{
				snprintf(label, sizeof(label), "%s##pad%d", name, other);
			}
			else
			{
				snprintf(label, sizeof(label), "%s  (from %s)##pad%d", name, kPlayerNames[other], other);
			}
			if (ComboItem(label, other == player) && (other != player))
			{
				NativeInput_SwapPlayers(player, other);
			}
		}
		TouchScrollWindow();
		ImGui::EndCombo();
	}
	HelpTooltip("The controller this player uses. Taking one from another player swaps their controllers. "
	            "The choice is remembered, and the controller rejoins this player when it reconnects.");

	if (!connected && (input.controllerId[0] != '\0'))
	{
		ImGui::TextDisabled("Remembered: %s (not connected)", (input.controllerName[0] != '\0') ? input.controllerName : input.controllerId);
		ImGui::SameLine();
		char forget[32];
		snprintf(forget, sizeof(forget), "Forget##forget%d", player);
		if (ImGui::SmallButton(forget))
		{
			input.controllerId[0] = '\0';
			input.controllerName[0] = '\0';
			CommitInputSettings();
		}
		HelpTooltip("Stop keeping this player free for that controller.");
	}
}

void DrawBindingsSection(int player)
{
	NativePlayerInputSettings &input = g_settings.input.players[player];
	const int padType = LabelGamepadType(player);

	SectionHeading("Bindings");
	ImGui::TextDisabled(kTouchUi ? "Tap a binding, then press the new controller button. Back cancels."
	                             : "Click a binding, then press the new key or button. Esc cancels, Backspace clears.");

	// The touch layout shows controller bindings only, at full height so the page alone scrolls.
	const float cellWidth = ImGui::GetFontSize() * 6.5f;
	const float tableHeight = kTouchUi ? 0.0f : ImGui::GetFrameHeightWithSpacing() * 9.5f;
	const ImGuiTableFlags flags = (kTouchUi ? ImGuiTableFlags_None : ImGuiTableFlags_ScrollY) | ImGuiTableFlags_RowBg |
	                              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerH;
	if (ImGui::BeginTable("##bindings", kTouchUi ? 2 : 4, flags, ImVec2(0.0f, tableHeight)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("PS1 button", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5.5f);
		if (!kTouchUi)
		{
			ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, cellWidth);
			ImGui::TableSetupColumn("Alt key", ImGuiTableColumnFlags_WidthFixed, cellWidth);
		}
		ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthFixed, cellWidth * 1.2f);
		ImGui::TableHeadersRow();

		char text[64];
		for (int button = 0; button < NATIVE_PAD_BUTTON_COUNT; button++)
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(kPadButtonLabels[button]);

			for (int key = 0; !kTouchUi && (key < NATIVE_KEY_BINDINGS); key++)
			{
				ImGui::TableSetColumnIndex(1 + key);
				FormatKey(input.keyboard[button][key], text, sizeof(text));
				BindingCell(player, button, key, text, cellWidth);
			}

			ImGui::TableSetColumnIndex(kTouchUi ? 1 : 3);
			FormatPadBinding(input.gamepad[button], padType, text, sizeof(text));
			BindingCell(player, button, BIND_GAMEPAD, text, cellWidth * 1.2f);
		}
		ImGui::EndTable();
	}
}

bool KeyUsedByOtherPlayer(int player, int scancode)
{
	for (int other = 0; (scancode != SDL_SCANCODE_UNKNOWN) && (other < NATIVE_INPUT_PLAYERS); other++)
	{
		for (int button = 0; (other != player) && (button < NATIVE_PAD_BUTTON_COUNT); button++)
		{
			for (int key = 0; key < NATIVE_KEY_BINDINGS; key++)
			{
				if (g_settings.input.players[other].keyboard[button][key] == scancode)
				{
					return true;
				}
			}
		}
	}
	return false;
}

void DrawAnalogSection(int player)
{
	NativePlayerInputSettings &input = g_settings.input.players[player];
	const float sliderWidth = ImGui::GetFontSize() * 12.0f;
	bool changed = false;

	SectionHeading("Analog & rumble");

	ImGui::SetNextItemWidth(sliderWidth);
	changed |= kTouchUi ? StepperInt("Stick deadzone", &input.stickDeadzone, 0, 50, 5, "%d%%")
	                    : ImGui::SliderInt("Stick deadzone", &input.stickDeadzone, 0, 50, "%d%%");
	HelpTooltip("Ignores small stick movements, for worn sticks that drift. 0% passes the stick through unchanged.");

	ImGui::SetNextItemWidth(sliderWidth);
	changed |= kTouchUi ? StepperInt("Analog press point", &input.analogPressPoint, 10, 90, 5, "%d%%")
	                    : ImGui::SliderInt("Analog press point", &input.analogPressPoint, 10, 90, "%d%%");
	HelpTooltip("How far a trigger or stick bound to a button must move before it counts as pressed.");

	changed |= CheckboxInt("Rumble", &input.rumble);
	ImGui::BeginDisabled(!input.rumble);
	ImGui::SetNextItemWidth(sliderWidth);
	changed |= kTouchUi ? StepperInt("Rumble strength", &input.rumbleStrength, 0, 100, 10, "%d%%")
	                    : ImGui::SliderInt("Rumble strength", &input.rumbleStrength, 0, 100, "%d%%");
	ImGui::EndDisabled();

	ImGui::Spacing();
	if (ImGui::Button("Reset this player"))
	{
		// Keeps the remembered controller; bindings and analog settings go back to the defaults.
		NativePlayerInputSettings defaults;
		NativeSettings_SetPlayerInputDefaults(&defaults, player);
		memcpy(defaults.controllerId, input.controllerId, sizeof(defaults.controllerId));
		memcpy(defaults.controllerName, input.controllerName, sizeof(defaults.controllerName));
		input = defaults;
		// Default keys another player has taken stay with that player.
		for (int button = 0; button < NATIVE_PAD_BUTTON_COUNT; button++)
		{
			for (int key = 0; key < NATIVE_KEY_BINDINGS; key++)
			{
				if (KeyUsedByOtherPlayer(player, input.keyboard[button][key]))
				{
					input.keyboard[button][key] = SDL_SCANCODE_UNKNOWN;
				}
			}
		}
		changed = true;
	}
	HelpTooltip(player == 0 ? "Restores the default keyboard and controller layout and analog settings for this player."
	                        : "Restores the default controller layout and analog settings, and clears this player's keys.");

	ImGui::SameLine();
	ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
	char copyId[32];
	snprintf(copyId, sizeof(copyId), "##copyfrom%d", player);
	if (ImGui::BeginCombo(copyId, "Copy controller layout from", ImGuiComboFlags_HeightRegular))
	{
		for (int other = 0; other < NATIVE_INPUT_PLAYERS; other++)
		{
			if ((other != player) && ComboItem(kPlayerNames[other], false))
			{
				const NativePlayerInputSettings &source = g_settings.input.players[other];
				memcpy(input.gamepad, source.gamepad, sizeof(input.gamepad));
				input.stickDeadzone = source.stickDeadzone;
				input.analogPressPoint = source.analogPressPoint;
				input.rumble = source.rumble;
				input.rumbleStrength = source.rumbleStrength;
				changed = true;
			}
		}
		TouchScrollWindow();
		ImGui::EndCombo();
	}
	HelpTooltip("Copies another player's controller bindings and analog settings. Keys stay as they are: each key drives one player.");

	if (changed)
	{
		CommitInputSettings();
	}
}

void DrawInputTab()
{
	ImGui::TextDisabled(kTouchUi ? "Each player has their own controller and settings."
	                             : "Each player has their own controller, keys and settings. One keyboard can drive several players.");

	if (ImGui::BeginTabBar("##players"))
	{
		for (int player = 0; player < NATIVE_INPUT_PLAYERS; player++)
		{
			const ImGuiTabItemFlags flags = (s_selectPlayerTab == player) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
			if (ImGui::BeginTabItem(kPlayerNames[player], nullptr, flags))
			{
				ImGui::PushID(player);
				DrawControllerSection(player);
				DrawBindingsSection(player);
				DrawAnalogSection(player);
				ImGui::PopID();
				ImGui::EndTabItem();
			}
		}
		ImGui::EndTabBar();
	}
	s_selectPlayerTab = -1;

	ImGui::TextDisabled("Home / PS button or L3 + R3 opens this menu from a controller. The game keeps running for everyone else.");
}

//------------------------------------------------------------------------------------------------------------------------
// Tweaks tab

constexpr const char *kFastBootNames[NATIVE_FAST_BOOT_COUNT] = {"Off", "Straight to title", "Straight to main menu"};
constexpr const char *kEngineClassNames[NATIVE_ENGINE_CLASS_COUNT] = {"Character's own", "Balanced", "Acceleration",
                                                                      "Speed",           "Turning",  "Drift (Nitro-Fueled)"};

// Returns whether a video setting changed (the caller applies it); other changes are saved here.
bool DrawTweaksGeneral()
{
	NativeGameSettings &game = g_settings.game;
	NativeVideoSettings &video = g_settings.video;
	const float comboWidth = ImGui::GetFontSize() * 12.0f;
	bool changed = false;
	bool videoChanged = false;

	SectionHeading("Boot");

	ImGui::SetNextItemWidth(comboWidth);
	changed |= ComboInt("Fast boot", &game.fastBoot, kFastBootNames, NATIVE_FAST_BOOT_COUNT);
	HelpTooltip("Skips the logo screens and the Naughty Dog crate intro; \"Straight to main menu\" also skips the title animation. "
	            "Takes effect the next time the game starts.");

	SectionHeading("Karts");

	// Drift is listed last, so leaving it out is a shorter list.
	int engineClass = NativeTweaks_EngineClass();
	ImGui::SetNextItemWidth(comboWidth);
	if (ComboInt("Engine class", &engineClass, kEngineClassNames, game.driftClass ? NATIVE_ENGINE_CLASS_COUNT : NATIVE_ENGINE_CLASS_DRIFT))
	{
		game.engineClass = engineClass;
		changed = true;
	}
	HelpTooltip("Every player's kart drives with this engine class, whichever character they pick: Acceleration gets up to speed "
	            "fastest, Speed has the highest top speed, Turning steers tightest. Opponents keep their own. "
	            "Takes effect from the next race.");

	if (CheckboxInt("Enable Drift class", &game.driftClass))
	{
		if (!game.driftClass && (game.engineClass == NATIVE_ENGINE_CLASS_DRIFT))
		{
			game.engineClass = NATIVE_ENGINE_CLASS_CHARACTER;
		}
		changed = true;
	}
	HelpTooltip("Adds Nitro-Fueled's Drift class to the engine classes: the tightest drifts of any class, paid for with slow "
	            "acceleration and a little less grip outside drifts. Its stats are PowerSlide's own take, not Nitro-Fueled's.");

	SectionHeading("Menus");

	videoChanged |= CheckboxInt("Full-colour pause screen", &video.pauseFullColor);
	HelpTooltip("Show the paused game in full colour behind the pause menu. Off uses the original sepia screenshot.");

	if (changed)
	{
		NativeSettings_Save();
	}
	return videoChanged;
}

void DrawTweaksCheats()
{
	NativeGameSettings &game = g_settings.game;
	bool changed = false;

	SectionHeading("Unlocks");

	changed |= CheckboxInt("Unlock all characters", &game.unlockAllCharacters);
	HelpTooltip("Every character can be picked at character select, including the secret ones. "
	            "Your save is not changed: turning this off locks them again.");

	// The game's own cheat codes, by group in table order, two to a row.
	ImGui::Spacing();
	ImGui::TextDisabled("The game's own cheat codes, as switches. Codes typed at the main menu still work.");
	for (int i = 0; i < NativeTweaks_CheatCount();)
	{
		const char *group = NativeTweaks_CheatAt(i)->group;
		SectionHeading(group);
		ImGui::PushID(group);
		if (ImGui::BeginTable("##cheats", 2, ImGuiTableFlags_SizingStretchSame))
		{
			for (; (i < NativeTweaks_CheatCount()) && (strcmp(NativeTweaks_CheatAt(i)->group, group) == 0); i++)
			{
				const NativeCheat *cheat = NativeTweaks_CheatAt(i);
				ImGui::TableNextColumn();
				changed |= CheckboxInt(cheat->label, cheat->setting);
				HelpTooltip(cheat->tooltip);
			}
			ImGui::EndTable();
		}
		ImGui::PopID();
	}

	ImGui::Spacing();
	if (ImGui::Button("Turn all cheats off"))
	{
		game.unlockAllCharacters = 0;
		for (int i = 0; i < NativeTweaks_CheatCount(); i++)
		{
			*NativeTweaks_CheatAt(i)->setting = 0;
		}
		changed = true;
	}
	HelpTooltip("Turns off every switch on this tab. Cheat codes typed at the main menu stay on until the game restarts.");

	if (changed)
	{
		NativeSettings_Save();
	}
}

void DrawTweaksTab()
{
	bool videoChanged = false;

	const bool ps1Mode = g_settings.global.ps1Mode != 0;

	Ps1ModeNotice();

	// PS1 mode locks the options, not the sub-tabs, so they can still be looked through.
	if (ImGui::BeginTabBar("##tweaks"))
	{
		if (ImGui::BeginTabItem("General"))
		{
			ImGui::BeginDisabled(ps1Mode);
			videoChanged |= DrawTweaksGeneral();
			ImGui::EndDisabled();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Cheats"))
		{
			ImGui::BeginDisabled(ps1Mode);
			DrawTweaksCheats();
			ImGui::EndDisabled();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	if (videoChanged)
	{
		CommitVideoSettings();
	}
}

//------------------------------------------------------------------------------------------------------------------------
// Global tab

// Preset last picked or saved; shown when several presets hold the same settings.
int s_lastPreset = -1;
bool s_namingPreset = false;
bool s_focusPresetName = false;
char s_presetName[NATIVE_PRESET_NAME_MAX] = "";
const char *s_presetError = nullptr;

// The preset the current settings match, or -1.
int MatchingPreset()
{
	if (NativePreset_Matches(s_lastPreset))
	{
		return s_lastPreset;
	}
	for (int i = 0; i < NativePreset_Count(); i++)
	{
		if (NativePreset_Matches(i))
		{
			return i;
		}
	}
	return -1;
}

void DrawPresetSection(float comboWidth)
{
	SectionHeading("Presets");

	const int current = MatchingPreset();
	ImGui::BeginDisabled(g_settings.global.ps1Mode != 0);

	ImGui::SetNextItemWidth(comboWidth);
	if (ImGui::BeginCombo("Preset", (current >= 0) ? NativePreset_Name(current) : "Custom (unsaved)"))
	{
		for (int i = 0; i < NativePreset_Count(); i++)
		{
			if (i == NATIVE_PRESET_BUILTIN_COUNT)
			{
				ImGui::Separator();
			}
			ImGui::PushID(i);
			if (ComboItem(NativePreset_Name(i), i == current))
			{
				NativePreset_Apply(i);
				s_lastPreset = i;
				s_customWindowSize = false;
				CommitVideoSettings();
			}
			ImGui::PopID();
		}
		TouchScrollWindow();
		ImGui::EndCombo();
	}
	HelpTooltip("Sets the Video picture options and the Tweaks tab in one go. Vanilla is the original game; Enhanced adds "
	            "widescreen, 4x resolution, frame rate matched to the display, a full-colour pause screen and fast boot. "
	            "Controls and the window are not changed.");

	if (!s_namingPreset)
	{
		if (ImGui::Button("Save as preset..."))
		{
			s_namingPreset = true;
			s_focusPresetName = true;
			s_presetError = nullptr;
			snprintf(s_presetName, sizeof(s_presetName), "%s", NativePreset_IsCustom(current) ? NativePreset_Name(current) : "");
		}
		HelpTooltip("Save the current options as a preset of your own. Saving with an existing name replaces that preset.");

		if (NativePreset_IsCustom(current))
		{
			ImGui::SameLine();
			if (ImGui::Button("Delete preset"))
			{
				NativePreset_DeleteCustom(current);
				s_lastPreset = -1;
			}
			char help[NATIVE_PRESET_NAME_MAX + 64];
			snprintf(help, sizeof(help), "Delete \"%s\". Your current options stay as they are.", NativePreset_Name(current));
			HelpTooltip(help);
		}
	}
	else
	{
		ImGui::SetNextItemWidth(comboWidth);
		if (s_focusPresetName)
		{
			ImGui::SetKeyboardFocusHere();
			s_focusPresetName = false;
		}
		const bool entered = ImGui::InputTextWithHint("##presetName", "Preset name", s_presetName, sizeof(s_presetName),
		                                              ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if (ImGui::Button("Save") || entered)
		{
			const int existing = NativePreset_Find(s_presetName);
			const int saved = NativePreset_SaveCustom(s_presetName);
			if (saved >= 0)
			{
				s_lastPreset = saved;
				s_namingPreset = false;
			}
			else if ((existing >= 0) && !NativePreset_IsCustom(existing))
			{
				s_presetError = "That name belongs to a built-in preset.";
			}
			else if (s_presetName[0] == '\0')
			{
				s_presetError = "Enter a name.";
			}
			else
			{
				s_presetError = "No room for more presets; delete one first.";
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			s_namingPreset = false;
		}
		if (s_presetError != nullptr)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, Color(kOrangeLight));
			ImGui::TextUnformatted(s_presetError);
			ImGui::PopStyleColor();
		}
	}

	ImGui::EndDisabled();
}

void DrawGlobalTab()
{
	const float comboWidth = ImGui::GetFontSize() * 12.0f;

	if (CheckboxInt("PS1 Mode", &g_settings.global.ps1Mode))
	{
		NativeSettings_RefreshPs1Mode();
		s_namingPreset = false;
		CommitVideoSettings();
	}
	HelpTooltip("Play exactly like the original PlayStation game: every picture, frame rate, menu and cheat option runs at its "
	            "original value, overriding presets. Your own choices are kept and come back when you turn this off. "
	            "Controls, the window and the FPS counter are not affected.");

	DrawPresetSection(comboWidth);
}

void DrawAboutTab()
{
	ImGui::Text("%s %s", NATIVE_PRODUCT_NAME, CTR_NATIVE_VERSION);
	ImGui::TextDisabled("Build %s", CTR_NATIVE_BUILD_ID);
	ImGui::Spacing();
	ImGui::TextWrapped("A native PC port of Crash Team Racing (NTSC-U), based on CTR Native and the CTR-ModSDK decompilation. "
	                   "Not affiliated with or endorsed by Sony Interactive Entertainment or Naughty Dog.");
	ImGui::Spacing();
	ImGui::TextDisabled("Settings file");
	if (NativeSettings_GetPath()[0] != '\0')
	{
		ImGui::TextWrapped("%s", NativeSettings_GetPath());
	}
	else
	{
		ImGui::TextWrapped("None: started with --vanilla, so changes are not saved.");
	}
	ImGui::Spacing();
	ImGui::TextDisabled("Dear ImGui %s  |  Fredoka font (SIL OFL 1.1)", IMGUI_VERSION);
	ImGui::Spacing();
	if (ImGui::Button("Quit game"))
	{
		SDL_Event quit = {};
		quit.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&quit);
	}

	// The launcher (platform/setup/, or the macOS app's setup window) sees the
	// request file at the next start and asks for a new disc image.
	if (s_discChangeEnabled)
	{
		ImGui::SameLine();
		if (ImGui::Button("Change disc image..."))
		{
			FILE *request = fopen(NATIVE_SETUP_REQUEST_FILE, "wb");
			if (request != NULL)
			{
				fclose(request);
				SDL_Event quit = {};
				quit.type = SDL_EVENT_QUIT;
				SDL_PushEvent(&quit);
			}
		}
		ImGui::TextDisabled("Quits; the next start asks for a disc image.");
	}
}

void DrawTitle()
{
	const float top = ImGui::GetCursorPosY();
	float height = ImGui::GetTextLineHeight();
	if ((s_logoTexture != nullptr) && (s_logoTexture->Status == ImTextureStatus_OK))
	{
		height = (float)s_logoTexture->Height;
		ImGui::Image(ImTextureRef(s_logoTexture->GetTexID()), ImVec2((float)s_logoTexture->Width, height));
	}
	else
	{
		ImGui::TextUnformatted(NATIVE_PRODUCT_NAME);
	}

	// Touch screens get a Close button at the right instead of the key hint (Back also closes).
	if (kTouchUi)
	{
		const char *label = "Close";
		const float width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 4.0f;
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width);
		ImGui::SetCursorPosY(top + SDL_max((height - ImGui::GetFrameHeight()) * 0.5f, 0.0f));
		if (ImGui::Button(label, ImVec2(width, 0.0f)))
		{
			SetMenuOpen(false, 0);
		}
		return;
	}

	// The hint sits at the right, centred on the logo.
	const char *hint = (s_menuController != 0) ? "Home / L3 + R3 to close" : "F1 / Esc to close";
	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(hint).x);
	ImGui::SetCursorPosY(top + (height - ImGui::GetTextLineHeight()) * 0.5f);
	ImGui::TextDisabled("%s", hint);
}

// One tab's page. In the touch layout the header and tabs stay put and only the page
// scrolls, by dragging.
void DrawTabPage(void (*draw)())
{
	if (!kTouchUi)
	{
		draw();
		return;
	}

	if (ImGui::BeginChild("##page"))
	{
		draw();
		TouchScrollWindow();
	}
	ImGui::EndChild();
}

void DrawMainWindow()
{
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
	if (kTouchUi)
	{
		// The full height of the safe area (clear of notches and rounded corners), at a readable width.
		ImVec2 pos = viewport->WorkPos;
		ImVec2 size = viewport->WorkSize;
		SDL_Rect safe = {};
		if (SDL_GetWindowSafeArea(s_window, &safe) && (safe.w > 0) && (safe.h > 0))
		{
			pos = ImVec2((float)safe.x, (float)safe.y);
			size = ImVec2((float)safe.w, (float)safe.h);
		}
		const float width = SDL_min(size.x, ImGui::GetFontSize() * kTouchMaxWidthFonts);
		ImGui::SetNextWindowPos(ImVec2(pos.x + (size.x - width) * 0.5f, pos.y));
		ImGui::SetNextWindowSize(ImVec2(width, size.y));
		flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
		        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	}
	else
	{
		// Stay centred as tabs of different sizes resize the window.
		ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 26.0f, 0.0f), viewport->WorkSize);
	}

	bool open = true;
	if (ImGui::Begin(NATIVE_PRODUCT_NAME, &open, flags))
	{
		DrawTitle();
		ImGui::Spacing();

		if (ImGui::BeginTabBar("##sections"))
		{
			if (ImGui::BeginTabItem("Video"))
			{
				DrawTabPage(DrawVideoTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Input"))
			{
				DrawTabPage(DrawInputTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Tweaks"))
			{
				DrawTabPage(DrawTweaksTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Global"))
			{
				DrawTabPage(DrawGlobalTab);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("About"))
			{
				DrawTabPage(DrawAboutTab);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();

	if (!open)
	{
		SetMenuOpen(false, 0);
	}
}

void DrawFpsOverlay()
{
	const ImGuiIO &io = ImGui::GetIO();
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	const float pad = ImGui::GetFontSize() * 0.6f;

	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad, viewport->WorkPos.y + pad), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
	ImGui::SetNextWindowBgAlpha(0.7f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad * 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
	                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
	if (ImGui::Begin("##fps", nullptr, flags))
	{
		ImGui::PushStyleColor(ImGuiCol_Text, Color(kYellow));
		ImGui::Text("%.1f FPS", io.Framerate);
		ImGui::PopStyleColor();
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}

void UpdateInputCapture()
{
	ImGuiIO &io = ImGui::GetIO();
	if (s_menuOpen)
	{
		io.ConfigFlags &= ~(ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoMouse);
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
		// Opened from a controller, the keyboard stays with the game.
		if (s_menuController == 0)
		{
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		}
		else
		{
			io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
		}
	}
	else
	{
		// Leave cursor visibility to the platform layer and ignore input while closed.
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NoMouse;
		io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
	}
}

} // namespace

extern "C" int NativeUI_Init(SDL_Window *window, SDL_GLContext context)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.LogFilename = nullptr;

	s_window = window;
	LoadFont();
	UpdateStyleScale();
	UpdateInputCapture();

	if (!ImGui_ImplSDL3_InitForOpenGL(window, context))
	{
		ImGui::DestroyContext();
		return 0;
	}

	// The renderer requests a 3.x core context; GLSL 1.50 is the core-profile floor.
	if (!ImGui_ImplOpenGL3_Init("#version 150"))
	{
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		return 0;
	}

	SetMenuController(0);
	s_initialized = true;
	return 1;
}

extern "C" void NativeUI_Shutdown(void)
{
	if (!s_initialized)
	{
		return;
	}

	s_initialized = false;
	DestroyLogoTexture();
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
}

extern "C" void NativeUI_ProcessEvent(const SDL_Event *event)
{
	if (!s_initialized)
	{
		return;
	}

	// Forget the menu's controller before the input layer closes it.
	if ((event->type == SDL_EVENT_GAMEPAD_REMOVED) && (event->gdevice.which == s_menuController))
	{
		SetMenuController(0);
	}

	// A pending rebind swallows the key or button it is waiting for, so the menu
	// doesn't also navigate with it.
	if (s_menuOpen && HandleCaptureEvent(event))
	{
		return;
	}
	ImGui_ImplSDL3_ProcessEvent(event);

	// A lifted finger leaves nothing hovered, so no tooltip or highlight stays behind.
	// ImGui still sees the release at the touch position first.
	if (kTouchUi && (event->type == SDL_EVENT_MOUSE_BUTTON_UP) && (event->button.which == SDL_TOUCH_MOUSEID))
	{
		ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
	}
}

extern "C" int NativeUI_IsCapturingBinding(void)
{
	return (s_menuOpen && s_capture.active) ? 1 : 0;
}

extern "C" void NativeUI_Render(void)
{
	if (!s_initialized)
	{
		return;
	}

	UpdateStyleScale();
	UpdateInputCapture();

	ImGui_ImplOpenGL3_NewFrame();
	if (s_menuOpen)
	{
		UpdateLogoTexture();
	}
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	if (s_menuOpen)
	{
		DrawMainWindow();
	}

	if (g_settings.video.showFps)
	{
		DrawFpsOverlay();
	}

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

extern "C" int NativeUI_IsMenuOpen(void)
{
	return s_menuOpen ? 1 : 0;
}

extern "C" void NativeUI_EnableDiscChange(void)
{
	s_discChangeEnabled = true;
}

extern "C" void NativeUI_ToggleMenu(void)
{
	SetMenuOpen(!s_menuOpen, 0);
}

extern "C" void NativeUI_ToggleMenuFromController(unsigned int instanceId)
{
	const SDL_JoystickID controller = (SDL_JoystickID)instanceId;
	if (s_menuOpen && (s_menuController == controller))
	{
		SetMenuOpen(false, 0);
		return;
	}

	SetMenuOpen(true, controller);
	for (int player = 0; player < NATIVE_INPUT_PLAYERS; player++)
	{
		if ((SDL_JoystickID)NativeInput_GetPlayerControllerId(player) == controller)
		{
			s_selectPlayerTab = player;
		}
	}
}

extern "C" int NativeUI_MenuOwnsKeyboard(void)
{
	return (s_menuOpen && (s_menuController == 0)) ? 1 : 0;
}

extern "C" unsigned int NativeUI_GetMenuController(void)
{
	return s_menuOpen ? (unsigned int)s_menuController : 0u;
}
