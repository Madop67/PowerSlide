// PowerSlide in-game settings menu built on Dear ImGui (externals/imgui).
// F1 (or Esc while open) toggles one tabbed settings window, styled after
// the game's own orange/yellow-on-blue menus. Overlays such as the FPS counter
// draw whether or not the menu is open.

#include <platform/native_ui.h>
#include <platform/native_product.h>
#include <platform/native_settings.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "native_ui_font.h"

#include <stdio.h>

#ifndef CTR_NATIVE_VERSION
#define CTR_NATIVE_VERSION "0.0.0-dev"
#endif

#ifndef CTR_NATIVE_BUILD_ID
#define CTR_NATIVE_BUILD_ID "unknown"
#endif

namespace
{

// Font size for a kReferenceHeight-tall window; the whole UI scales with the
// window height (never below kMinHeightScale) and with display DPI.
constexpr float kBaseFontSize = 16.0f;
constexpr float kReferenceHeight = 720.0f;
constexpr float kMinHeightScale = 0.75f;
constexpr float kTitleFontScale = 1.6f;

struct WindowSizePreset
{
	const char *label;
	int width;
	int height;
};

constexpr WindowSizePreset kWindowSizes[] = {
	{"640 x 480 (4:3)", 640, 480},
	{"960 x 720 (4:3)", 960, 720},
	{"1280 x 960 (4:3)", 1280, 960},
	{"1440 x 1080 (4:3)", 1440, 1080},
	{"1280 x 720 (16:9)", 1280, 720},
	{"1600 x 900 (16:9)", 1600, 900},
	{"1920 x 1080 (16:9)", 1920, 1080},
};

constexpr const char *kDisplayModeNames[NATIVE_DISPLAY_COUNT] = {"Windowed", "Fullscreen"};
constexpr const char *kAspectModeNames[NATIVE_ASPECT_COUNT] = {"Original (4:3)", "Stretch to window"};
constexpr const char *kScaleFilterNames[NATIVE_SCALE_FILTER_COUNT] = {"Sharp (nearest)", "Smooth (bilinear)"};

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
float s_styleScale = 0.0f;

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

void UpdateStyleScale()
{
	float scale = SDL_GetWindowDisplayScale(s_window);
	if (scale <= 0.0f)
	{
		scale = 1.0f;
	}

	int width = 0;
	int height = 0;
	SDL_GetWindowSize(s_window, &width, &height);
	if (height > 0)
	{
		// Follow the window height in eighth steps so a window drag doesn't rebuild the
		// style every pixel; the floor keeps small windows readable.
		const float heightScale = SDL_floorf((height / kReferenceHeight) * 8.0f) / 8.0f;
		scale *= SDL_max(heightScale, kMinHeightScale);
	}

	if (scale != s_styleScale)
	{
		ApplyStyle(scale);
	}
}

void SetMenuOpen(bool open)
{
	if (s_menuOpen == open)
	{
		return;
	}
	s_menuOpen = open;
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

void HelpTooltip(const char *text)
{
	ImGui::SetItemTooltip("%s", text);
}

void SectionHeading(const char *text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, Color(kYellow));
	ImGui::SeparatorText(text);
	ImGui::PopStyleColor();
}

void DrawVideoTab()
{
	NativeVideoSettings &video = g_settings.video;
	bool changed = false;
	const float comboWidth = ImGui::GetFontSize() * 12.0f;

	SectionHeading("Display");

	ImGui::SetNextItemWidth(comboWidth);
	changed |= ImGui::Combo("Display mode", &video.displayMode, kDisplayModeNames, NATIVE_DISPLAY_COUNT);
	HelpTooltip("Fullscreen uses the desktop resolution (borderless). F11 or Alt+Enter also toggles it.");

	int currentWidth = 0;
	int currentHeight = 0;
	SDL_GetWindowSize(s_window, &currentWidth, &currentHeight);
	char sizePreview[32];
	snprintf(sizePreview, sizeof(sizePreview), "%d x %d", currentWidth, currentHeight);

	ImGui::BeginDisabled(video.displayMode != NATIVE_DISPLAY_WINDOWED);
	ImGui::SetNextItemWidth(comboWidth);
	if (ImGui::BeginCombo("Window size", sizePreview))
	{
		for (const WindowSizePreset &preset : kWindowSizes)
		{
			const bool selected = (preset.width == currentWidth) && (preset.height == currentHeight);
			if (ImGui::Selectable(preset.label, selected))
			{
				video.windowWidth = preset.width;
				video.windowHeight = preset.height;
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip(video.displayMode == NATIVE_DISPLAY_WINDOWED ? "You can also drag the window edges to any size." : "Switch to windowed mode to pick a window size.");

	SectionHeading("Picture");

	ImGui::SetNextItemWidth(comboWidth);
	char scalePreview[32];
	snprintf(scalePreview, sizeof(scalePreview), video.renderScale <= 1 ? "1x (original)" : "%dx", video.renderScale);
	if (ImGui::BeginCombo("Internal resolution", scalePreview))
	{
		for (int scale = 1; scale <= NATIVE_RENDER_SCALE_MAX; scale++)
		{
			char label[48];
			// 512 x 216 is the in-race display; menus use other PS1 display sizes.
			snprintf(label, sizeof(label), scale == 1 ? "1x (original, 512 x 216)" : "%dx (%d x %d)", scale, 512 * scale, 216 * scale);
			if (ImGui::Selectable(label, video.renderScale == scale))
			{
				video.renderScale = scale;
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	HelpTooltip("Renders the game at a multiple of the original resolution for sharper edges. "
	            "Above 1x the final image also skips the PS1's 15-bit colour reduction. Screen effects (blur, heat haze) stay at 1x.");

	ImGui::SetNextItemWidth(comboWidth);
	changed |= ImGui::Combo("Aspect ratio", &video.aspectMode, kAspectModeNames, NATIVE_ASPECT_COUNT);
	HelpTooltip("Original keeps the 4:3 picture with black bars. Stretch fills the window and distorts the image.");

	ImGui::BeginDisabled(video.aspectMode != NATIVE_ASPECT_ORIGINAL);
	changed |= CheckboxInt("Integer scaling", &video.integerScaling);
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Scale by whole multiples of the original line count, so every line is the same height. May add wider borders.");

	ImGui::SetNextItemWidth(comboWidth);
	changed |= ImGui::Combo("Scaling filter", &video.scaleFilter, kScaleFilterNames, NATIVE_SCALE_FILTER_COUNT);
	HelpTooltip("How the picture is enlarged to fit the window. Sharp keeps hard pixel edges; smooth blends them.");

	changed |= CheckboxInt("Texture filtering", &video.textureFiltering);
	HelpTooltip("Bilinear filtering of in-game textures. Not part of the original look.");

	SectionHeading("Overlay");
	changed |= CheckboxInt("Show FPS counter", &video.showFps);

	ImGui::Spacing();
	if (ImGui::Button("Reset video settings"))
	{
		const int keepWidth = video.windowWidth;
		const int keepHeight = video.windowHeight;
		NativeSettings defaults;
		NativeSettings_SetDefaults(&defaults);
		video = defaults.video;
		video.windowWidth = keepWidth;
		video.windowHeight = keepHeight;
		changed = true;
	}
	HelpTooltip("Restores every video option except the window size.");

	if (changed)
	{
		CommitVideoSettings();
	}
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
	ImGui::TextWrapped("%s", NativeSettings_GetPath());
	ImGui::Spacing();
	ImGui::TextDisabled("Dear ImGui %s  |  Fredoka font (SIL OFL 1.1)", IMGUI_VERSION);
	ImGui::Spacing();
	if (ImGui::Button("Quit game"))
	{
		SDL_Event quit = {};
		quit.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&quit);
	}
}

void DrawTitle()
{
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kTitleFontScale);
	ImGui::PushStyleColor(ImGuiCol_Text, Color(kYellow));
	ImGui::TextUnformatted("POWER");
	ImGui::PopStyleColor();
	ImGui::SameLine(0.0f, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_Text, Color(kOrange));
	ImGui::TextUnformatted("SLIDE");
	ImGui::PopStyleColor();
	ImGui::PopFont();

	const char *hint = "F1 / Esc to close";
	ImGui::SameLine();
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(hint).x);
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled("%s", hint);
}

void DrawMainWindow()
{
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(ImGui::GetFontSize() * 26.0f, 0.0f), viewport->WorkSize);

	bool open = true;
	const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
	if (ImGui::Begin(NATIVE_PRODUCT_NAME, &open, flags))
	{
		DrawTitle();
		ImGui::Spacing();

		if (ImGui::BeginTabBar("##sections"))
		{
			if (ImGui::BeginTabItem("Video"))
			{
				DrawVideoTab();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("About"))
			{
				DrawAboutTab();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
	ImGui::End();

	if (!open)
	{
		SetMenuOpen(false);
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
		const float fps = io.Framerate;
		ImGui::PushStyleColor(ImGuiCol_Text, Color(kYellow));
		ImGui::Text("%.1f FPS", fps);
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::TextDisabled("%.1f ms", fps > 0.0f ? 1000.0f / fps : 0.0f);
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
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
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
	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
}

extern "C" void NativeUI_ProcessEvent(const SDL_Event *event)
{
	if (s_initialized)
	{
		ImGui_ImplSDL3_ProcessEvent(event);
	}
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

extern "C" void NativeUI_ToggleMenu(void)
{
	SetMenuOpen(!s_menuOpen);
}
