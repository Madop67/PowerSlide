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
- **Keep vanilla vanilla: run the replay suite.** After any change to game code, timing, input or rendering, run `python3 tools/replays/replay_suite.py run` (also the `ctr_replay_suite` ctest). It plays power-on replays from `debug/replay-suite/` with default settings and fails on any divergence. Add `-- --set <cvar>=<value>` to check that an enhancement leaves game logic alone. See `docs/REPLAYS.md`.
- **Hooks, not calls.** An enhancement subscribes to a hook in `include/platform/native_hooks.h` from its own file (register it in `NativeHooks_RegisterModules`). Add a new hook to the `NATIVE_HOOKS` list when no existing one fits; game code calls `NATIVE_HOOK(Name, ...)` inside `#if defined(CTR_NATIVE)`.
- **Settings are CVars.** A new setting is a field in `struct NativeSettings` plus one row in `s_fixedDescs` (`platform/native_settings.c`); that gives it a `ctr-native.toml` key and `--set <section>.<key>=<value>`. Defaults equal retail.
- **Branding is user-facing only.** The name comes from `include/platform/native_product.h`. Code, file and target names (`ctr_native.exe`, `[CTR Native]` log prefixes, the log file name) keep the upstream naming so merges stay cheap.
- **Keep the sandbox builds working, even when you can't test them.** macOS, the Linux x86_64 release and Android run the game as a wasm32 sandbox (see Environment), so every platform change also has to work there. When you can't build one, still write the sandbox side:
  - New GL call: rerun `python3 tools/macos/gen_wasm_gl_bridge.py` and commit the regenerated `guest_gl.h` and `host_gl.gen.c`. Pointer-to-pointer arguments and pointer returns need a hand-written shim; buffer-offset "pointers" go in `OFFSET_POINTER_ARGS`.
  - New SDL call (in `platform/` or `native_ui.cpp`): add it to `platform/wasm/imports.syms` and add a `w2c_env_SDL_*` shim in `platform/wasm_host/host_sdl.c`. Opaque objects become handles (`HostHandle_*`), and returned strings or arrays are copied into guest memory.
  - New SDL event type with pointer members: translate it in `w2c_env_SDL_PollEvent`; don't memcpy it.
  - New source files: a C file reaches the guest through `main.c`'s unity build. A C++ file must also be added to `CTR_GUEST_CXX_SOURCES` in `cmake/ctr_wasm_sandbox.cmake`.
  - No threads, `fork`/`exec`, `dlopen` or host-pointer-sized assumptions in guest code. Guest pointers are 32-bit.
  - Android has OpenGL ES 3.0 only. A new GL call that ES lacks, or one taking a desktop-only enum or format, needs a shim in `platform/wasm_host/host_gles.c` (redirected in `host_gles.h`); shaders keep compiling as both GLSL 1.40 and GLSL ES 3.00 (the host rewrites `#version`).
  - Say in the commit or PR which sandbox builds are untested.
- **Ship no game data.** `assets/` (the user's `ctr-u.bin` and extracted files) is git-ignored. Never commit Sony or Naughty Dog material, including the PlayStation logo.
- **Commits:** branch first (no direct work on `master`), use conventional-commit style subjects (`feat(video): ...`), and add the `Co-Authored-By` trailer. The Windows checkout has no git identity configured; commit with `git -c user.name=Madop67 -c user.email=madop67@gmail.com commit ...`.

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
  - `./build-macos.sh` fetches the pinned wasi-sdk/WABT (`tools/wasm/fetch-toolchain.sh`, macOS arm64 and Linux x86_64 hosts), builds `build-macos/ctr_native` and `build-macos/PowerSlide.app`, runs ctest, then `./package-macos.sh` makes `dist/PowerSlide-<version>-macos-arm64.dmg` (`--no-dmg` skips it).
  - The bundle's data folder is `~/Library/Application Support/PowerSlide` (disc in `assets/`, settings, memcards, `ctr_native-console.log`). Hold Option at launch to re-run disc setup. The bare `build-macos/ctr_native` never runs setup and uses the repo's `assets/`.
  - The game and the ImGui menu compile to wasm32, go through wasm2c, and link with a native host (`platform/wasm_host/`). See `cmake/ctr_wasm_sandbox.cmake`.
  - A new GL call needs `python3 tools/macos/gen_wasm_gl_bridge.py`. A new SDL call needs an entry in `platform/wasm/imports.syms` and a shim in `platform/wasm_host/host_sdl.c`; the guest link fails with `undefined symbol` until both exist. SDL events with pointer members must be translated, not memcpy'd (see `HostSDL_TranslateTextInput`).
  - `./match.sh check` does not run on macOS; use WSL/Linux.
  - Android builds here too: `./build-android.sh` (Android Studio's SDK, NDK, CMake and JDK at their default paths) writes `dist/PowerSlide-<version>-android.apk`. The `Medium_Phone_API_36.1` AVD runs it (`emulator -avd Medium_Phone_API_36.1 -gpu swiftshader_indirect`); use the SDK's `adb` (`~/Library/Android/sdk/platform-tools/adb`), not Homebrew's. App data: `/sdcard/Android/data/io.github.madop67.powerslide/files`.
- **Release packages (all standalone, first-run disc setup into a per-user data folder):**
  - Windows: `build-msvc.bat --package` → `dist/PowerSlide-<version>-windows-x86.zip` (`PowerSlide.exe`, a GUI-subsystem exe; data in `%APPDATA%\PowerSlide`; Shift at launch or `--setup` re-runs setup).
  - Linux: `./build-linux-x64.sh` → `dist/PowerSlide-<version>-linux-x86_64.tar.gz` (x86_64 wasm sandbox build in `build-linux-x64/`; data in `~/.local/share/PowerSlide`). Build it on ubuntu-22.04 (glibc 2.35) or older for wide distro reach.
  - macOS: `./build-macos.sh` → DMG. Android: `./build-android.sh` → APK.
  - Setup only runs when no `assets/` is found next to (or up to two folders above) the executable, so checkouts, dev builds and the replay suite keep the portable layout. `tools/release/check_standalone.py` (ctest `ctr_native_standalone`) fails a build that links the CRT, SDL, libchdr, X11 and so on dynamically.
- **Disc and references:**
  - `assets/ctr-u.bin` is the user's NTSC-U disc (SHA256 `f780bf23…`).
  - `python3 tools/matching/extract_references.py` extracts `SCUS_944.26` and overlays 221–233 from it.
- **CI:** `.github/workflows/ci.yml` runs on GitHub Actions: Linux i686 (dev build + toolchain check), Linux x86_64 release tarball, Windows x86 release zip and Android APK.
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
| Branding | Logo (`docs/images/powerslide-logo.png`): README header, menu header (box-filtered on the CPU to the drawn size, uploaded through `ImGui_ImplOpenGL3_UpdateTexture`), window icon ("PS" from the wordmark, `SDL_SetWindowIcon`) and Windows exe icon (`platform/windows/ctr_native.rc`). `tools/branding/make_branding.py` (Pillow) regenerates `platform/native_branding_data.h` (palette + RLE) and `powerslide.ico`; the build never runs it | `platform/native_branding.c`, `platform/native_ui.cpp`, `platform/native_renderer.c` |
| Input | Input tab: rebind every PS1 button (2 keys and 1 controller input each; click, then press; Esc cancels, Backspace clears); keyboard player; per-player controller assignment; stick deadzone; analog press point; rumble on/off and strength. Saved under `[input]` by key and controller names. The Home/PS button or L3+R3 opens the menu. Bindings are table-driven; defaults equal the old hard-coded layout | `platform/native_input.c`, `platform/native_ui.cpp`, `include/platform/native_input_config.h` |
| Input | Per-player input: each player has its own keys (one keyboard can drive several players), controller layout, deadzone, press point and rumble, saved under `[input.player1]`..`[input.player4]`. An old shared `[input]` section is converted on load. Controllers are remembered per player by USB vendor:product id and rejoin that player on reconnect. The menu no longer freezes everyone's input: it holds back only the device that opened it (keyboard, or that one controller, which alone navigates it) | `platform/native_input.c`, `platform/native_settings.c`, `platform/native_ui.cpp` |
| Build | macOS Apple Silicon build via a wasm32 sandbox (wasi-sdk → wasm2c → native arm64), including the ImGui settings menu | `cmake/ctr_wasm_sandbox.cmake`, `platform/wasm/`, `platform/wasm_host/`, `tools/macos/` |
| Build | macOS app bundle and DMG. `PowerSlide.app` (exe still `ctr_native`, ad-hoc signed; Developer ID + notarization via `CODESIGN_IDENTITY`/`NOTARY_PROFILE`). On first launch (or with Option held) an AppKit setup window imports the user's `.cue`/`.bin`/`.chd` into Application Support: CHD decoded by vendored libchdr in the host, image checked for the PVD, `BIGFILE.BIG` and `SCUS_944.26`, then passed to the guest as `--base-dir` (new, all platforms). A non-zero guest exit or trap shows an alert with Show Log. `make_branding.py` also writes `PowerSlide.icns` | `platform/wasm_host/host_setup_macos.m`, `platform/wasm_host/host_disc_import.c`, `platform/macos/`, `package-macos.sh`, `main.c`, `externals/libchdr` |
| Build | Standalone Windows, Linux and Android releases with macOS-style first-run disc setup. Shared SDL setup (`platform/setup/native_setup.c`: message box, system file picker or drag-and-drop, SDL_Renderer progress window, per-user data folder, console log, error box with Show Log) and disc import moved to `platform/setup/disc_import.c` (SDL IO, so Windows UTF-16 paths and Android `content://` URIs work; CHD through libchdr callbacks). Windows: GUI subsystem, UTF-8 manifest, crash box. Linux ships as an x86_64 wasm sandbox build; Android is a new sandbox target (NDK `libmain.so` in SDLActivity, GL 3.3 → GLES 3.0 shims, Back = menu, forced landscape fullscreen). Menu About tab: "Change disc image..." (writes `setup-requested`). `check_standalone.py` and CI jobs for every package | `platform/setup/`, `main.c`, `platform/wasm_host/`, `android/`, `cmake/ctr_wasm_sandbox.cmake`, `build-*.sh`, `package-*`, `tools/release/`, `tools/wasm/` |
| UI | Touch layout of the menu for Android (`CTR_NATIVE_TOUCH_UI`, set on the Android guest only; desktop menus are unchanged): sized by display density (about 18 dp text, 44 dp rows), full safe-area height (`SDL_GetWindowSafeArea`) with a fixed header (Close button) and tabs over a page that scrolls by dragging, with fling (`TouchScrollWindow`; a drag lets go of the item under the finger). Sliders become -/+ steppers that repeat when held, dropdown rows are full height, help opens from a "?" button, a lifted finger leaves nothing hovered. Window options and keyboard bindings are hidden; Back cancels a rebind | `platform/native_ui.cpp`, `cmake/ctr_wasm_sandbox.cmake`, `platform/wasm_host/host_sdl.c` |
| Testing | Power-on replays (`--record --from-boot`): no checkpoint, boot VBlanks in a prelude block, so any build and platform can play them. `--record-frames`, `--replay-check` (exit 3 = diverged), `--replay-turbo` (~30× speed), `--input-script` (scripted pad 1), `--screenshot-every`. Frame checks include a driver-state hash (replay format v2). Suite tool and ctest `ctr_replay_suite`; standard suite = attract mode (boot, intro, 2 demo races), Crash Cove time trial, Crash Cove arcade race (~10 min of play, ~20 s to check) | `platform/native_replay_scheduler.c`, `tools/replays/`, `docs/REPLAYS.md` |
| Settings | CVar registry: every setting is `<section>.<key>`; `NativeCVar_Find/SetFromText/Format`. `--set name=value` (repeatable) and `--vanilla` (defaults, never saves) | `platform/native_settings.c` |
| Hooks | Hook bus: X-macro list of typed hooks (`Boot`, `TitleIntro`, `BeforeDraw`, `AfterPresent`, `CharacterUnlocked`); frame interpolation runs on it | `include/platform/native_hooks.h`, `platform/native_hooks.c` |
| QoL | Fast boot (`game.fast_boot`, Tweaks tab): `title` skips SCEA, copyright and the crate; `menu` also skips the title animation (menu in ~1 s instead of ~40). Off while recording or replaying | `platform/native_fast_boot.c`, `game/MAIN/MainMain.c`, `game/LOAD/LOAD_TenStages.c`, `game/230/MM_MenuFlow.c` |
| Tweaks | The menu's Game tab is now Tweaks (settings keep the `[game]` section). It holds fast boot, the full-colour pause screen (moved from Video), and cheats: unlock all characters (`game.unlock_all_characters`, via the `CharacterUnlocked` hook in character select; the save is not changed), unlimited turbo and unlimited wumpa (`game.unlimited_turbo` / `game.unlimited_wumpa`: switch on retail's `CHEAT_TURBO` / `CHEAT_WUMPA` each tick, not in time trial; only flags the module set are cleared) | `platform/native_tweaks.c`, `game/230/MM_Characters.c`, `platform/native_ui.cpp` |
| Tweaks | Tweaks has General and Cheats sub-tabs (PS1 Mode locks the options, not the sub-tabs). Cheats lists every retail cheat code as a toggle, from the `s_cheats` table in `native_tweaks.c` (exposed by `native_tweaks.h`; the UI draws it in a loop): wumpa, turbo, masks, bombs, super engine, invisibility, icy tracks, super turbo pads, one lap, super hard, harder adventure, turbo counter (`game.<name>`, all `P`). As in retail, the item cheats (wumpa, turbo, masks, bombs, engine) stay off in adventure and time trial | `platform/native_tweaks.c`, `include/platform/native_tweaks.h`, `platform/native_ui.cpp` |
| Global | Global tab. PS1 Mode (`global.ps1_mode`) runs every preset setting (CVar flag `NATIVE_CVAR_FLAG_PRESET`: picture, frame rate, Tweaks) at retail values; the chosen values are kept aside and still saved (`NativeSettings_RefreshPs1Mode`). Presets: built-in Vanilla and Enhanced, plus up to 16 custom ones in `ctr-native-presets.toml` (`NativePreset_*`). Controls, window, display mode and FPS counter are outside presets | `platform/native_settings.c`, `platform/native_ui.cpp` |
| Fix | Build-dependent AI: a bot on a quad without a checkpoint (index 0xff) made `VehLap_UpdateProgress` read restart point 255, far past the table, into host pointers. Any change to the exe layout (even adding const data) changed that bot's progress and, through rubber-banding, the race; attract mode diverged. Native now skips the update for out-of-range indices, as retail does for players | `game/Vehicle/VehLap.c` |
| Fix | Widescreen crash in the attract intro: four 1P clip-record writers overran the clip buffer into level data; they now check space like the others | `game/226/226_00_DrawLevelOvr1P.c` |
| Fix | Linux builds use 64-bit file offsets (32-bit `stat()` failed on XFS/btrfs/WSL `/mnt/c`) | `CMakeLists.txt` |

The debug wireframe toggle moved from F1 to Shift+F1. F2–F12 keep their upstream debug uses.

## macOS port fixes (branch `fix/macos-port`, merged)

Merging `feature/macos-port` with the settings, video and input work left macOS with the menu stubbed out and a failing version test. The fix:
- The sandbox ctest version check now expects `PowerSlide`.
- The guest compiles `main.c` (C) and `native_ui.cpp` + Dear ImGui (C++, `-fno-exceptions -fno-rtti`, `IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS`) to separate wasm objects and links them with wasi-sdk `clang++`. The embedded UI font block in `CMakeLists.txt` now runs before the sandbox branch so both builds get it.
- ImGui's OpenGL3 backend uses `native_glad.h` → `guest_gl.h` as its custom loader (`IMGUI_IMPL_OPENGL_LOADER_CUSTOM`, force-included). glad stops at GL 3.1, so ImGui compiles out `glBindSampler`, `glDrawElementsBaseVertex` and `glClipControl` on its own. The GL bridge generator now scans the UI and backend sources, passes the `glDrawElements` index offset through, and skips the ES2-only `glGetVertexAttribPointerv`.
- 25 new SDL host imports for the menu and ImGui's SDL3 backend (cursors, clipboard, text input, window queries, `SDL_PushEvent`, ...). `SDL_EVENT_TEXT_INPUT` is rebuilt in wasm32 layout, with the text in a reusable guest buffer (`HostSDL_TranslateTextInput`).
- Verified on an M4: clean build, ctest 2/2, the game runs about 30 fps, and the menu renders. Mouse, keyboard and gamepad menu interaction still need a manual check.

## Next tasks (in order)

Phase 1 (hook bus, CVars and settings file, ImGui menu, render scale, rebinding, fast boot; gate: vanilla passes every replay) is done. The suite also passes with widescreen (16:9, 21:9), 60 fps and 2× render scale. Phase 2 next:

1. **Phase 2 gate in CI terms:** run the suite with `-- --set video.frame_rate=60` (and 120/144) before merging any interpolation change.
2. **Widescreen clip buffer:** in very wide views the 1P renderer now drops near-plane polygons once its clip buffer is full instead of corrupting memory. Check for holes near the camera at 21:9+; if any, allocate a larger clip buffer when widescreen is on (`MainDB_GetClipSize`, mind the mempack budget).
3. **More suite coverage:** a scripted lap with drifting and boosts, a 2-player split-screen race, adventure hub, battle. Scripts live in `tools/replays/scripts/`; `record_suite.ps1` records them.
4. Remaining P1 features from the plan doc: presets, analog tuning, mixer, quick restart, extra HUD, save management, bug-fix toggles, cheats and developer tools.

## Later ideas

- Make quick-load reject states from a different build with a clear message, instead of crashing.
- Edge-anchored widescreen HUD: test 3–4 player split screen.
- PGXP-style subpixel precision for less vertex wobble at high internal resolutions.
- Texture packs, music replacement, a mod loader, cheats, a debug tools tab.
