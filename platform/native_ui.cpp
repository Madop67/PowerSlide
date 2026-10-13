// In-game settings menu built on Dear ImGui (externals/imgui).
// F1 toggles a menu bar; settings windows hang off it. Overlays such as the
// FPS counter draw whether or not the menu is open.

#include <platform/native_ui.h>
#include <platform/native_settings.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"

#include <stdio.h>

#ifndef CTR_NATIVE_VERSION
#define CTR_NATIVE_VERSION "0.0.0-dev"
#endif

#ifndef CTR_NATIVE_BUILD_ID
#define CTR_NATIVE_BUILD_ID "unknown"
#endif

namespace
{

// The default font is designed for 13 px; the UI grows past that only for
// windows taller than kReferenceHeight (and for high-DPI displays).
constexpr float kBaseFontSize = 13.0f;
constexpr float kReferenceHeight = 720.0f;

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

SDL_Window *s_window = nullptr;
bool s_initialized = false;
bool s_menuOpen = false;
bool s_showVideoWindow = true;
bool s_showAboutWindow = false;
float s_styleScale = 0.0f;

void ApplyStyle(float scale)
{
	ImGuiStyle &style = ImGui::GetStyle();
	style = ImGuiStyle();
	ImGui::StyleColorsDark(&style);

	style.WindowRounding = 6.0f;
	style.ChildRounding = 4.0f;
	style.FrameRounding = 4.0f;
	style.PopupRounding = 4.0f;
	style.GrabRounding = 4.0f;
	style.WindowPadding = ImVec2(12.0f, 12.0f);
	style.FramePadding = ImVec2(8.0f, 4.0f);
	style.ItemSpacing = ImVec2(8.0f, 6.0f);
	style.ScaleAllSizes(scale);

	style.FontSizeBase = kBaseFontSize;
	style._NextFrameFontSizeBase = kBaseFontSize;
	style.FontScaleDpi = scale;
	s_styleScale = scale;
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
	if (height > kReferenceHeight)
	{
		// Quarter steps keep the font from re-baking on every pixel of a window drag.
		scale *= SDL_floorf((height / kReferenceHeight) * 4.0f) / 4.0f;
	}

	if (scale != s_styleScale)
	{
		ApplyStyle(scale);
	}
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

void DrawVideoWindow()
{
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->GetWorkCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), viewport->WorkSize);
	if (!ImGui::Begin("Video Settings", &s_showVideoWindow, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	NativeVideoSettings &video = g_settings.video;
	bool changed = false;
	const float comboWidth = ImGui::GetFontSize() * 14.0f;

	ImGui::SeparatorText("Display");

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

	ImGui::SeparatorText("Picture");

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

	ImGui::SeparatorText("Overlay");
	changed |= CheckboxInt("Show FPS counter", &video.showFps);

	ImGui::Separator();
	if (ImGui::Button("Reset to defaults"))
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

	ImGui::TextDisabled("Changes apply and save immediately.");

	if (changed)
	{
		CommitVideoSettings();
	}

	ImGui::End();
}

void DrawAboutWindow()
{
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
	if (ImGui::Begin("About CTR Native", &s_showAboutWindow, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse))
	{
		ImGui::Text("CTR Native %s (%s)", CTR_NATIVE_VERSION, CTR_NATIVE_BUILD_ID);
		ImGui::TextDisabled("Native PC port of Crash Team Racing (NTSC-U).");
		ImGui::TextDisabled("Settings file: %s", NativeSettings_GetPath());
		ImGui::TextDisabled("Dear ImGui %s", IMGUI_VERSION);
	}
	ImGui::End();
}

void DrawMenuBar()
{
	if (!ImGui::BeginMainMenuBar())
	{
		return;
	}

	if (ImGui::BeginMenu("CTR Native"))
	{
		ImGui::MenuItem("About", nullptr, &s_showAboutWindow);
		ImGui::Separator();
		if (ImGui::MenuItem("Quit"))
		{
			SDL_Event quit = {};
			quit.type = SDL_EVENT_QUIT;
			SDL_PushEvent(&quit);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Settings"))
	{
		ImGui::MenuItem("Video", nullptr, &s_showVideoWindow);
		ImGui::EndMenu();
	}

	const char *hint = "F1 closes this menu";
	const float hintWidth = ImGui::CalcTextSize(hint).x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
	ImGui::SameLine(ImGui::GetWindowWidth() - hintWidth);
	ImGui::TextDisabled("%s", hint);

	ImGui::EndMainMenuBar();
}

void DrawFpsOverlay()
{
	const ImGuiIO &io = ImGui::GetIO();
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	const float pad = ImGui::GetFontSize() * 0.6f;
	const float top = viewport->WorkPos.y + pad;

	ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - pad, top), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
	ImGui::SetNextWindowBgAlpha(0.45f);
	const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
	                               ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
	if (ImGui::Begin("##fps", nullptr, flags))
	{
		const float fps = io.Framerate;
		ImGui::Text("%.1f FPS  %.1f ms", fps, fps > 0.0f ? 1000.0f / fps : 0.0f);
	}
	ImGui::End();
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
		DrawMenuBar();
		if (s_showVideoWindow)
		{
			DrawVideoWindow();
		}
		if (s_showAboutWindow)
		{
			DrawAboutWindow();
		}
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
	s_menuOpen = !s_menuOpen;
	if (s_menuOpen && !s_showVideoWindow && !s_showAboutWindow)
	{
		s_showVideoWindow = true;
	}
}
