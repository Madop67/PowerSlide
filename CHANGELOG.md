# Changelog

All notable changes to PowerSlide. Every new feature is off by default, so the
game plays like the original unless you turn something on.

## 1.1.0 (2026-10-16)

### New

- **24-kart races** (Tweaks → Races): single-player arcade and adventure races and
  cups start with 23 opponents instead of 7. Boss races, the purple gem cup, time
  trial, relic races, battle and multiplayer keep the normal field. Places 9th to
  24th show as text on the HUD, results list all 24 karts in two rows, and cup
  standings show the top 7 plus you.
- **Engine class** (Tweaks): race every one of your karts as balanced,
  acceleration, speed or turning, whatever the character, or as a new drift class
  (more slide, tighter drifts). Opponents keep their own.
- **Dual Screen** tab: a DS-style second screen for dual-screen handhelds (such as
  the AYN Thor) or a second monitor. Shows the track map with every kart, race
  standings with items, Wumpa and speed, or lap times with your best and the
  difference. Use it on another display or as an inset panel in the game window,
  tap the page strip to switch pages, and optionally hide the minimap on the main
  screen. On Android, the second display uses the device's other screen.

### Fixed

- 24-kart races barely moved at frame rates above 30 fps (60 fps, or matched to
  the display), which looked like the game had frozen.
- In widescreen with the HUD at the screen edges, a sliver of the Wumpa fruit
  icon stayed behind next to the item box.
- In widescreen, the results screen's icons and lap times did not slide all the
  way off the screen.
- At internal resolutions above 1×, levels that clear the screen to a sky colour
  (Coco Park) showed garbage at the top of the screen where the sky does not reach.
- Unlock all characters now also shows the unlocked characters' icons in character
  select.

## 1.0.0 (2026-10-15)

First release: Windows, macOS, Linux and Android packages with first-run disc
setup, an in-game settings menu (Video, Input, Tweaks, Global, About), higher
internal resolutions, widescreen, frame rates up to 240 fps, per-player input
rebinding, fast boot, every original cheat as a switch, presets and PS1 Mode.
