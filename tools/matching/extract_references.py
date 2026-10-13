#!/usr/bin/env python3
"""Extract the matching reference files from a retail NTSC-U disc image.

Reads ``assets/ctr-u.bin`` (single-track MODE2/2352) and writes the
executable and BIGFILE overlays named in the matching manifest, checking
each against its recorded SHA256.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
import sys
from pathlib import Path

from pipeline import DEFAULT_MANIFEST, MatchError, load_json, reference_root

RAW_SECTOR = 2352
USER_DATA = 2048
MODE2_FORM1_HEADER = 24
BIGFILE_SECTOR = 2048


class DiscImage:
    def __init__(self, path: Path) -> None:
        self.data = path.read_bytes()
        if len(self.data) % RAW_SECTOR:
            raise MatchError(f"{path} is not a raw 2352-byte-sector image")

    def sector(self, lba: int) -> bytes:
        start = lba * RAW_SECTOR + MODE2_FORM1_HEADER
        return self.data[start : start + USER_DATA]

    def read(self, lba: int, size: int) -> bytes:
        count = (size + USER_DATA - 1) // USER_DATA
        return b"".join(self.sector(lba + i) for i in range(count))[:size]

    def find(self, path: str) -> tuple[int, int]:
        """Return (lba, size) of an ISO9660 file such as 'SCUS_944.26'."""
        pvd = self.sector(16)
        if pvd[1:6] != b"CD001":
            raise MatchError("no ISO9660 primary volume descriptor at sector 16")
        lba, size = struct.unpack_from("<I", pvd, 156 + 2)[0], struct.unpack_from("<I", pvd, 156 + 10)[0]
        for part in path.strip("/").split("/"):
            lba, size = self._lookup(lba, size, part.upper())
        return lba, size

    def _lookup(self, lba: int, size: int, name: str) -> tuple[int, int]:
        directory = self.read(lba, size)
        offset = 0
        while offset < len(directory):
            length = directory[offset]
            if length == 0:
                offset = (offset // USER_DATA + 1) * USER_DATA
                continue
            record = directory[offset : offset + length]
            ident = record[33 : 33 + record[32]].decode("ascii", "replace")
            if re.sub(r";\d+$", "", ident) == name:
                return struct.unpack_from("<I", record, 2)[0], struct.unpack_from("<I", record, 10)[0]
            offset += length
        raise MatchError(f"{name} not found on disc")


def bigfile_entry(bigfile: bytes, index: int) -> bytes:
    _, count = struct.unpack_from("<ii", bigfile, 0)
    if not 0 <= index < count:
        raise MatchError(f"BIGFILE entry {index} out of range ({count} entries)")
    offset, size = struct.unpack_from("<ii", bigfile, 8 + index * 8)
    start = offset * BIGFILE_SECTOR
    return bigfile[start : start + size]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", default=str(DEFAULT_MANIFEST))
    parser.add_argument("--reference-root")
    args = parser.parse_args()

    manifest = load_json(Path(args.manifest))
    root = reference_root(manifest, args.reference_root)
    artifacts = {a["id"]: a for a in manifest["artifacts"]}

    disc = DiscImage(root / artifacts["disc"]["path"])
    exe_lba, exe_size = disc.find(artifacts["exe"]["path"])
    big_lba, big_size = disc.find("BIGFILE.BIG")
    bigfile = disc.read(big_lba, big_size)

    outputs = {"exe": disc.read(exe_lba, exe_size)}
    for artifact in manifest["artifacts"]:
        if artifact["kind"] == "overlay":
            outputs[artifact["id"]] = bigfile_entry(bigfile, int(artifact["id"]))

    ok = True
    for artifact_id, blob in outputs.items():
        artifact = artifacts[artifact_id]
        digest = hashlib.sha256(blob).hexdigest()
        if digest != artifact["sha256"]:
            ok = False
            print(f"FAIL    {artifact_id}: sha256 {digest[:12]}, expected {artifact['sha256'][:12]}")
            continue
        path = root / artifact["path"]
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(blob)
        print(f"OK      {artifact_id}: {artifact['path']} ({len(blob)} bytes)")
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except MatchError as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
