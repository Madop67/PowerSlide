#!/usr/bin/env python3
"""Replay regression suite: power-on input replays that must play back without divergence.

A suite is a folder of replays, one per sub-folder (input.ctrreplay plus memcard.seed),
default debug/replay-suite. Replays hold only inputs, timing and per-frame state checks,
recorded from power-on with --from-boot, so any build can play them. The replays and
memcard seeds come from your own disc and saves and stay out of git.

  replay_suite.py add <report-folder> <name>   copy a recording into the suite
  replay_suite.py run [--exe PATH]             play every replay, fail on any divergence
  replay_suite.py info <input.ctrreplay>       header and level timeline of one replay

Record a new one (Windows: build-msvc-x86/Release/ctr_native.exe):
  build/ctr_native --vanilla --record --from-boot --record-frames 9000
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_SUITE = os.path.join(REPO, "debug", "replay-suite")

FILE_MAGIC = 0x52525443
FRAME_MAGIC = 0x4D524652
PRELUDE_MAGIC = 0x444C5250
HEADER = struct.Struct("<10I64s32s3I")
FRAME_INFO_FIELDS = (
    "frameTimer frameCounter timer framesInThisLEV elapsedTimeMS msInThisLEV elapsedEventTime "
    "mainGameState loadingStage levelID mixRandomNumber audioRNG deadcoed0 deadcoed1 advRng0 advRng1 driverHash"
).split()
FRAME_INFO = struct.Struct("<10i7I")
PAD_SNAPSHOT_SIZE = 12
PAD_COUNT = 4
VSYNC_PACKETS = 64
FRAME_SIZE = 8 + 2 * FRAME_INFO.size + PAD_COUNT * PAD_SNAPSHOT_SIZE + 8 + VSYNC_PACKETS * 2 + 8
POLICIES = {0: "rolling", 1: "bootstrap-only", 2: "power-on"}

EXIT_CODES = {0: "pass", 2: "error", 3: "diverged"}


def level_names():
    names = {}
    path = os.path.join(REPO, "include", "namespace_Level.h")
    try:
        with open(path, encoding="utf-8") as header:
            text = header.read()
    except OSError:
        return names
    body = text[text.index("enum LevelID") :]
    body = body[body.index("{") + 1 : body.index("}")]
    value = -1
    for match in re.finditer(r"^\s*([A-Z0-9_]+)\s*(?:=\s*(\d+))?\s*,", body, re.M):
        value = int(match.group(2)) if match.group(2) else value + 1
        names[value] = match.group(1)
    return names


def read_replay(path):
    with open(path, "rb") as replay:
        data = replay.read()
    fields = HEADER.unpack_from(data, 0)
    header = {
        "magic": fields[0],
        "version": fields[1],
        "headerSize": fields[2],
        "frameRecordSize": fields[3],
        "frameCount": fields[4],
        "checkpointCount": fields[5],
        "policy": POLICIES.get(fields[9], str(fields[9])),
        "buildId": fields[10].split(b"\0")[0].decode(errors="replace"),
        "platform": fields[11].split(b"\0")[0].decode(errors="replace"),
        "preludeSize": fields[12],
    }
    if header["magic"] != FILE_MAGIC:
        raise ValueError(f"{path}: not a replay file")
    if header["frameRecordSize"] != FRAME_SIZE:
        raise ValueError(f"{path}: replay version {header['version']} is not supported by this tool")

    offset = header["headerSize"]
    header["preludePackets"] = 0
    if header["preludeSize"]:
        magic, count = struct.unpack_from("<2I", data, offset)
        if magic == PRELUDE_MAGIC:
            header["preludePackets"] = count
        offset += header["preludeSize"]

    frames = []
    for index in range(header["frameCount"]):
        base = offset + index * FRAME_SIZE
        if base + FRAME_SIZE > len(data):
            break
        magic, _ = struct.unpack_from("<2I", data, base)
        if magic != FRAME_MAGIC:
            break
        end = FRAME_INFO.unpack_from(data, base + 8 + FRAME_INFO.size)
        frames.append(dict(zip(FRAME_INFO_FIELDS, end)))
    return header, frames


def level_timeline(frames):
    timeline = []
    for index, frame in enumerate(frames):
        if not timeline or timeline[-1][1] != frame["levelID"]:
            timeline.append((index, frame["levelID"]))
    return timeline


def describe(path):
    header, frames = read_replay(path)
    names = level_names()
    minutes = len(frames) / 30.0 / 60.0
    print(f"{path}")
    print(f"  {header['policy']} replay v{header['version']}, {len(frames)} frames (~{minutes:.1f} min), "
          f"recorded by {header['buildId']} on {header['platform']}")
    if header["policy"] == "power-on":
        print(f"  boot prelude: {header['preludePackets']} VBlank packets")
    for index, level in level_timeline(frames):
        print(f"  frame {index:6d}: {names.get(level, level)}")


def default_exe():
    candidates = [
        os.path.join(REPO, "build-msvc-x86", "Release", "ctr_native.exe"),
        os.path.join(REPO, "build-msvc-x86", "RelWithDebInfo", "ctr_native.exe"),
        os.path.join(REPO, "build", "ctr_native"),
        os.path.join(REPO, "build", "ctr_native.exe"),
    ]
    existing = [path for path in candidates if os.path.exists(path)]
    return max(existing, key=os.path.getmtime) if existing else None


def find_replays(suite):
    replays = []
    if not os.path.isdir(suite):
        return replays
    for name in sorted(os.listdir(suite)):
        path = os.path.join(suite, name, "input.ctrreplay")
        if os.path.isfile(path):
            replays.append((name, path))
    return replays


def run(args):
    exe = args.exe or default_exe()
    if not exe or not os.path.exists(exe):
        print("replay suite: no ctr_native build found; pass --exe", file=sys.stderr)
        return 2
    replays = find_replays(args.suite)
    if not replays:
        print(f"replay suite: no replays in {args.suite}; skipping")
        return args.skip_code

    failures = 0
    print(f"replay suite: {len(replays)} replay(s), {exe}")
    for name, path in replays:
        header, _ = read_replay(path)
        command = [exe, "--vanilla", "--replay", os.path.abspath(path), "--replay-check"]
        if not args.realtime:
            command.append("--replay-turbo")
        command.extend(args.extra)
        timeout = 120 + header["frameCount"] / 30.0 * 2
        started = time.monotonic()
        try:
            result = subprocess.run(command, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout,
                                    stdin=subprocess.DEVNULL)
            code = result.returncode
            output = result.stdout.decode(errors="replace")
        except subprocess.TimeoutExpired as expired:
            code = None
            output = (expired.stdout or b"").decode(errors="replace")
        elapsed = time.monotonic() - started

        status = "timeout" if code is None else EXIT_CODES.get(code, f"exit {code}")
        if code != 0 or "replay finished after" not in output:
            failures += 1
            if code == 0:
                status = "incomplete"
        print(f"  {status:10s} {name} ({header['frameCount']} frames, {elapsed:.1f}s)")
        if status != "pass":
            for line in output.splitlines():
                if "[CTR Replay]" in line and ("diverg" in line or "expected" in line or "live" in line or "fail" in line):
                    print(f"      {line.strip()}")

    print(f"replay suite: {len(replays) - failures} passed, {failures} failed")
    return 1 if failures else 0


def add(args):
    source = args.report
    replay = os.path.join(source, "input.ctrreplay")
    seed = os.path.join(source, "memcard.seed")
    if not os.path.isfile(replay) or not os.path.isdir(seed):
        print(f"replay suite: {source} has no input.ctrreplay and memcard.seed", file=sys.stderr)
        return 2
    header, _ = read_replay(replay)
    if header["policy"] != "power-on":
        print("replay suite: only power-on replays (recorded with --from-boot) work across builds", file=sys.stderr)
        return 2
    target = os.path.join(args.suite, args.name)
    if os.path.exists(target):
        print(f"replay suite: {target} already exists", file=sys.stderr)
        return 2
    os.makedirs(target)
    shutil.copy2(replay, target)
    shutil.copytree(seed, os.path.join(target, "memcard.seed"))
    if os.path.isfile(os.path.join(source, "metadata.txt")):
        shutil.copy2(os.path.join(source, "metadata.txt"), target)
    describe(os.path.join(target, "input.ctrreplay"))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--suite", default=DEFAULT_SUITE, help="suite folder (default debug/replay-suite)")
    commands = parser.add_subparsers(dest="command", required=True)

    run_parser = commands.add_parser("run", help="play every replay in the suite")
    run_parser.add_argument("--exe", help="ctr_native binary (default: newest local build)")
    run_parser.add_argument("--realtime", action="store_true", help="play at normal speed instead of --replay-turbo")
    run_parser.add_argument("--skip-code", type=int, default=0, help="exit code when the suite is empty (ctest uses 77)")
    run_parser.add_argument("extra", nargs="*", help="extra arguments for ctr_native, after --")

    add_parser = commands.add_parser("add", help="copy a recording into the suite")
    add_parser.add_argument("report", help="report folder from --record --from-boot")
    add_parser.add_argument("name", help="name for the replay in the suite")

    info_parser = commands.add_parser("info", help="describe one replay")
    info_parser.add_argument("replay")

    args = parser.parse_args()
    if args.command == "run":
        return run(args)
    if args.command == "add":
        return add(args)
    describe(args.replay)
    return 0


if __name__ == "__main__":
    sys.exit(main())
