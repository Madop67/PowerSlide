# Replays

Use this for bug reports in internal builds.

## Quick State

- `F5`: save `debug/states/quick.ctrstates`
- `F8`: load `debug/states/quick.ctrstates`

## Record

```sh
build/ctr_native --record
```

Windows: use `build\ctr_native.exe` instead.

Normal saves live in `memcards/slot0`.

When recording starts, the CTR save files from `memcards/slot0` and `slot1` are copied to `memcard.seed`. The game records with a writable copy named `memcard.recording`, so saves and ghosts made while recording stay in the report.

To choose when recording starts:

```sh
build/ctr_native --record --toggle
```

- Press `F9` to start.
- Press `F10` to stop.

For more detailed reports:

```sh
build/ctr_native --record --detailed
```

You can combine both:

```sh
build/ctr_native --record --toggle --detailed
```

## Play Back

Use the command written in that folder's `metadata.txt`.

It looks like:

```sh
build/ctr_native --replay "debug/reports/20260605/ctr-123456/input.ctrreplay"
```

Playback creates a fresh writable `memcard.playback` from `memcard.seed` every run and does not touch your real saves.

If a developer asks you to bypass header identity checks:

```sh
build/ctr_native --replay "debug/reports/20260605/ctr-123456/input.ctrreplay" --replay-bypass-header
```

## Regression Suite (power-on replays)

Normal replays start from a memory checkpoint, so they only play in the exact build
that recorded them. A **power-on** replay starts before the game boots and holds only
pad input, VBlank timing and per-frame state checks, so any later build can play it.
That makes it a regression test: if a change alters game logic, playback diverges.

Record one with retail settings (`--vanilla` ignores `ctr-native.toml`):

```sh
build/ctr_native --vanilla --record --from-boot --record-frames 9000
```

- `--from-boot`: power-on replay. Only with `--record`, not with `--toggle` or `--detailed`.
- `--record-frames N`: finish the recording and quit after N frames (30 per second).
- `--input-script FILE`: pad 1 follows a script instead of your input, for repeatable
  recordings. Each line is `<frame> <buttons>` and holds those buttons (`cross`,
  `dpad_left`, `l1` ...; `-` for none) from that frame on. See `tools/replays/scripts/`.
- `--screenshot-every N`: saves `debug/shots/frame-NNNNNN.bmp` every N frames, to see
  what a script did. Counts replay frames while recording or playing back, and
  presented frames otherwise.

Play back as a check:

```sh
build/ctr_native --vanilla --replay "<folder>/input.ctrreplay" --replay-check --replay-turbo
```

- `--replay-check`: stop at the first divergence. Exit code 0 = no divergence,
  2 = the replay could not be played, 3 = diverged.
- `--replay-turbo`: play the recorded VBlanks back to back, not in real time
  (about 30× faster).

Each frame compares timers, game state, level, every RNG stream, and a hash of
every driver's position, speed, angle, lap and items.

`tools/replays/replay_suite.py` manages a suite in `debug/replay-suite/` (git-ignored:
replays come from your own disc and memory card):

```sh
python3 tools/replays/replay_suite.py add debug/reports/<date>/<folder> my-replay
python3 tools/replays/replay_suite.py run          # also runs as the ctr_replay_suite ctest
python3 tools/replays/replay_suite.py info <folder>/input.ctrreplay
```

On Windows, `tools/replays/record_suite.ps1` records the standard suite (attract
mode, a time trial and an arcade race; about 10 minutes in real time). The suite
must pass before and after every change to game code or timing.
