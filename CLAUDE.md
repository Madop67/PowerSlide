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
- **Keep the macOS build working, even when you can't test it.** macOS runs the game as a wasm32 sandbox (see Environment), so every platform change also has to work there. When you can't build on a Mac, still write the macOS side:
  - New GL call: rerun `python3 tools/macos/gen_wasm_gl_bridge.py` and commit the regenerated `guest_gl.h` and `host_gl.gen.c`. Pointer-to-pointer arguments and pointer returns need a hand-written shim; buffer-offset "pointers" go in `OFFSET_POINTER_ARGS`.
  - New SDL call (in `platform/` or `native_ui.cpp`): add it to `platform/wasm/imports.syms` and add a `w2c_env_SDL_*` shim in `platform/wasm_host/host_sdl.c`. Opaque objects become handles (`HostHandle_*`), and returned strings or arrays are copied into guest memory.
  - New SDL event type with pointer members: translate it in `w2c_env_SDL_PollEvent`; don't memcpy it.
  - New source files: a C file reaches the guest through `main.c`'s unity build. A C++ file must also be added to `CTR_GUEST_CXX_SOURCES` in `cmake/ctr_wasm_sandbox.cmake`.
  - No threads, `fork`/`exec`, `dlopen` or host-pointer-sized assumptions in guest code. Guest pointers are 32-bit.
  - Say in the commit or PR when the macOS side is untested.
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
- **macOS checkout:** `~/workspace/ctr-decomp/ctr-native` (Apple Silicon).
  - `./build-macos.sh` fetches the pinned wasi-sdk/WABT (`tools/macos/fetch-toolchain.sh`), builds `build-macos/ctr_native` and runs ctest.
  - The game and the ImGui menu compile to wasm32, go through wasm2c, and link with a native host (`platform/wasm_host/`). See `cmake/ctr_wasm_sandbox.cmake`.
  - A new GL call needs `python3 tools/macos/gen_wasm_gl_bridge.py`. A new SDL call needs an entry in `platform/wasm/imports.syms` and a shim in `platform/wasm_host/host_sdl.c`; the guest link fails with `undefined symbol` until both exist. SDL events with pointer members must be translated, not memcpy'd (see `HostSDL_TranslateTextInput`).
  - `./match.sh check` does not run on macOS; use WSL/Linux.
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
| Video | Display mode, window size (presets follow the aspect ratio, plus custom), aspect (original, 16:9, 21:9, custom 4:3–32:9, stretch), integer scaling, sharp/smooth filter, texture filtering, FPS overlay. Frame rate 20–240 or match display (`Platform_GetTargetFrameRate`); below 30 some ticks are not presented | `platform/native_platform.c`, `platform/native_renderer.c` |
| Video | Internal resolution 1×–8×, or a custom fractional scale up to 16×: the main render target scales; VRAM and offscreen targets stay at 1×; above 1× the frame is presented from the high-res target | `platform/native_renderer.c` |
| Video | Widescreen (any ratio wider than 4:3): view-projection X × (4/3)/aspect (`NativeSettings_WideSqueeze1000`, 0.75 at 16:9), wider frustum and far clip (after ModSDK's `USE_16BY9`) | `game/PushBuffer.c` |
| Video | Widescreen HUD: the GPU layer squeezes HUD OT slots and menu sub-viewports by the same factor at draw time. The race HUD moves to the screen edges via primitive-memory anchor ranges | `platform/native_gpu.c`, `game/MAIN/MainFrame.c`, `game/MAIN/MainFrame_RenderFrame.c` |
| Branding | "PowerSlide" in the window title, `--version`, menu and README | `include/platform/native_product.h` |
| Input | Input tab: rebind every PS1 button (2 keys and 1 controller input each; click, then press; Esc cancels, Backspace clears); keyboard player; per-player controller assignment; stick deadzone; analog press point; rumble on/off and strength. Saved under `[input]` by key and controller names. The Home/PS button or L3+R3 opens the menu. Bindings are table-driven; defaults equal the old hard-coded layout | `platform/native_input.c`, `platform/native_ui.cpp`, `include/platform/native_input_config.h` |
| Input | Per-player input: each player has its own keys (one keyboard can drive several players), controller layout, deadzone, press point and rumble, saved under `[input.player1]`..`[input.player4]`. An old shared `[input]` section is converted on load. Controllers are remembered per player by USB vendor:product id and rejoin that player on reconnect. The menu no longer freezes everyone's input: it holds back only the device that opened it (keyboard, or that one controller, which alone navigates it) | `platform/native_input.c`, `platform/native_settings.c`, `platform/native_ui.cpp` |
| Build | macOS Apple Silicon build via a wasm32 sandbox (wasi-sdk → wasm2c → native arm64), including the ImGui settings menu | `cmake/ctr_wasm_sandbox.cmake`, `platform/wasm/`, `platform/wasm_host/`, `tools/macos/` |

The debug wireframe toggle moved from F1 to Shift+F1. F2–F12 keep their upstream debug uses.

## macOS port fixes (branch `fix/macos-port`, merged)

Merging `feature/macos-port` with the settings, video and input work left macOS with the menu stubbed out and a failing version test. The fix:
- The sandbox ctest version check now expects `PowerSlide`.
- The guest compiles `main.c` (C) and `native_ui.cpp` + Dear ImGui (C++, `-fno-exceptions -fno-rtti`, `IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS`) to separate wasm objects and links them with wasi-sdk `clang++`. The embedded UI font block in `CMakeLists.txt` now runs before the sandbox branch so both builds get it.
- ImGui's OpenGL3 backend uses `native_glad.h` → `guest_gl.h` as its custom loader (`IMGUI_IMPL_OPENGL_LOADER_CUSTOM`, force-included). glad stops at GL 3.1, so ImGui compiles out `glBindSampler`, `glDrawElementsBaseVertex` and `glClipControl` on its own. The GL bridge generator now scans the UI and backend sources, passes the `glDrawElements` index offset through, and skips the ES2-only `glGetVertexAttribPointerv`.
- 25 new SDL host imports for the menu and ImGui's SDL3 backend (cursors, clipboard, text input, window queries, `SDL_PushEvent`, ...). `SDL_EVENT_TEXT_INPUT` is rebuilt in wasm32 layout, with the text in a reusable guest buffer (`HostSDL_TranslateTextInput`).
- Verified on an M4: clean build, ctest 2/2, the game runs about 30 fps, and the menu renders. Mouse, keyboard and gamepad menu interaction still need a manual check.

## Next tasks (in order)

1. **Input configuration menu: done** (branches `feature/input-config`, `feature/input-per-player`). The game deliberately keeps running while the menu is open. Two identical controllers share an id, so they fill remembered players in connection order.
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
