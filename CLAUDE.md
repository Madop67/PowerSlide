# PowerSlide

PowerSlide is a feature-rich native PC port of Crash Team Racing (PS1, NTSC-U), in the spirit of Ship of Harkinian: an in-game settings menu, modern video options, and later input, 60 fps, mods and extras. It is a fork of [CTR-tools/ctr-native](https://github.com/CTR-tools/ctr-native), which builds on the CTR-ModSDK decompilation.

- **Fork:** <https://github.com/Madop67/ctr-native>
- **Plan doc:** <https://claude.ai/code/artifact/704e1d05-af33-43cc-b35d-9744225b87eb>

## Ground rules

- **Never push to CTR-tools/ctr-native.** Push only to `origin` (the Madop67 fork). The `upstream` push URL is deliberately set to `DISABLED-do-not-push-to-upstream`.
- **Vanilla by default.** Every enhancement is a setting whose default (or off state) behaves like retail. Widescreen is the one exception: once on, its HUD default is "Screen edges".
- **Keep the matching build byte-identical.**
  - Changes to `game/` go inside `#ifdef CTR_NATIVE` (or `#if defined(CTR_NATIVE)`), so the PS1 matching build never sees them.
  - Run `./match.sh check` after touching `game/`. It must report `175/175 configured targets matched`.
- **Branding is user-facing only.** The name comes from `include/platform/native_product.h`. Code, file and target names (`ctr_native.exe`, `[CTR Native]` log prefixes, the log file name) keep the upstream naming so merges stay cheap.
- **Ship no game data.** `assets/` (the user's `ctr-u.bin` and extracted files) is git-ignored. Never commit Sony or Naughty Dog material, including the PlayStation logo.
- **Commits:** branch first (no direct work on `master`), use conventional-commit style subjects (`feat(video): ...`), and add the `Co-Authored-By` trailer.

## Environment

- **Windows checkout:** `C:\Workspaces\ctr-decomp\ctr-native`. Builds with MSVC x86 (VS 2022 Build Tools).
  - Load the dev shell with `Enter-VsDevShell -VsInstallPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" -DevCmdArguments '-arch=x86 -host_arch=amd64'`.
  - Then run `cmake --preset windows-msvc-x86`, `cmake --build --preset windows-msvc-x86-release`, and `ctest --preset windows-msvc-x86-release`.
  - Output: `build-msvc-x86\Release\ctr_native.exe`. For a `.pdb` to symbolize crashes, build `--config RelWithDebInfo`.
- **WSL checkout:** Ubuntu at `~/ctr-native`.
  - `./build.sh` builds the 32-bit Linux binary and runs ctest.
  - `./match.sh check` runs the byte-matching pipeline; it needs Linux.
  - The WSL checkout syncs from the Windows one via `git fetch /mnt/c/Workspaces/ctr-decomp/ctr-native <branch>`.
  - Passwordless `sudo apt-get` is allowed (`/etc/sudoers.d/claude-apt`).
- **Disc and references:**
  - `assets/ctr-u.bin` is the user's NTSC-U disc (SHA256 `f780bf23…`).
  - `python3 tools/matching/extract_references.py` extracts `SCUS_944.26` and overlays 221–233 from it.
- **CI:** `.github/workflows/ci.yml` runs on GitHub Actions, building and testing Linux i686 and Windows x86.
- **GPU in WSL:** the 32-bit build under WSL renders with llvmpipe, because WSL's d3d12 GPU passthrough is 64-bit only. Judge visuals and performance on the Windows build.
- **Quick states (F5 save / F8 load)** store raw memory addresses. They only load in the exact build that saved them; loading in another build crashes.

## What has been added (branches `phase0/local-setup` → `feature/video-settings`)

| Area | Change | Main files |
| --- | --- | --- |
| Tooling | Extract reference exe and overlays from the disc | `tools/matching/extract_references.py` |
| Build | Linux `-m32` finds i386 pkg-config files, which fixes PulseAudio | `build.sh` |
| Build | MSVC fixes: GCC statement-expression, nested zero-length array, Windows path tests | `game/SubmitName.c`, `game/230/MM_Characters.c`, `tools/matching/test_match.py` |
| CI | GitHub Actions for Linux i686 and Windows x86 | `.github/workflows/ci.yml` |
| Settings | `ctr-native.toml` in the base directory (small TOML subset) with a table-driven loader and saver | `platform/native_settings.c`, `include/platform/native_settings.h` |
| UI | Dear ImGui 1.92.1 menu (F1 or Esc). One tabbed window (Video, About) in CTR colours, with embedded Fredoka SemiBold (OFL). Built as a C++ static lib, `ctr_native_ui` | `platform/native_ui.cpp`, `externals/imgui`, `externals/fonts` |
| Input | The game sees released buttons and centred sticks while the menu is open; pads stay connected | `platform/native_input.c` |
| Video | Display mode, window size, aspect (original, widescreen, stretch), integer scaling, sharp/smooth filter, texture filtering, FPS overlay | `platform/native_platform.c`, `platform/native_renderer.c` |
| Video | Internal resolution 1×–8×: the main render target scales; VRAM and offscreen targets stay at 1×; above 1× the frame is presented from the high-res target | `platform/native_renderer.c` |
| Video | Widescreen 16:9: view-projection X × 0.75, wider frustum and far clip (after ModSDK's `USE_16BY9`) | `game/PushBuffer.c` |
| Video | Widescreen HUD: the GPU layer squeezes HUD OT slots and menu sub-viewports to 3/4 width at draw time. The race HUD moves to the screen edges via primitive-memory anchor ranges | `platform/native_gpu.c`, `game/MAIN/MainFrame.c`, `game/MAIN/MainFrame_RenderFrame.c` |
| Branding | "PowerSlide" in the window title, `--version`, menu and README | `include/platform/native_product.h` |
| Input | Input tab: rebind every PS1 button (2 keys and 1 controller input each; click, then press; Esc cancels, Backspace clears); keyboard player; per-player controller assignment; stick deadzone; analog press point; rumble on/off and strength. Saved under `[input]` by key and controller names. The Home/PS button or L3+R3 opens the menu. Bindings are table-driven; defaults equal the old hard-coded layout | `platform/native_input.c`, `platform/native_ui.cpp`, `include/platform/native_input_config.h` |

The debug wireframe toggle moved from F1 to Shift+F1. F2–F12 keep their upstream debug uses.

## Next tasks (in order)

1. **Input configuration menu: done** (branch `feature/input-config`). Follow-ups:
   - Per-player controller bindings (today one controller layout is shared by all players).
   - Remember controller-to-player assignment by controller GUID.
   - Optionally pause the game while the menu is open (main-menu idle timers keep running).
2. **60 fps by interpolation.** The user chose this over porting ModSDK's `USE_60FPS`.
   - Game logic stays at 30 Hz (`vsyncTillFlip = 2`), so physics, ghosts and records stay retail.
   - Render extra frames by blending camera and instance transforms between the last two logic ticks, as Ship of Harkinian does.
   - Must not change replays or ghost determinism.
   - Should allow refresh rates above 60.

## Later ideas

- Make quick-load reject states from a different build with a clear message, instead of crashing.
- Edge-anchored widescreen HUD: test 3–4 player split screen.
- PGXP-style subpixel precision for less vertex wobble at high internal resolutions.
- Texture packs, music replacement, a mod loader, cheats, a debug tools tab.
