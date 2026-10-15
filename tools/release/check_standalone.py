#!/usr/bin/env python3
"""Check that a release build runs without libraries a player would have to install.

    check_standalone.py <ctr_native.exe | ctr_native | PowerSlide.apk>

- Windows (PE): no MSVC/UCRT redistributables, MinGW runtime DLLs, SDL or
  libchdr DLLs. System DLLs (KERNEL32, USER32, OPENGL32, ...) are fine.
- Linux (ELF): only glibc, libGL and the dynamic loader. SDL loads X11,
  Wayland and the audio libraries itself at run time. Also prints the newest
  glibc symbol version the binary needs, which sets the oldest distro it runs on.
- Android (APK): only libmain.so, linked against libraries every device has.

macOS keeps its own otool check in package-macos.sh.

Pure Python (no readelf/dumpbin), so it runs on any build host.
"""

import fnmatch
import re
import struct
import sys
import zipfile
from pathlib import Path

PE_FORBIDDEN = [
    "vcruntime*.dll",
    "msvcp*.dll",
    "msvcr*.dll",
    "concrt*.dll",
    "ucrtbase*.dll",
    "api-ms-win-crt-*.dll",
    "sdl3*.dll",
    "libgcc*.dll",
    "libstdc++*.dll",
    "libwinpthread*.dll",
    "zlib*.dll",
    "libzstd*.dll",
    "*chdr*.dll",
]

LINUX_ALLOWED = [
    "libc.so.6",
    "libm.so.6",
    "libdl.so.2",
    "libpthread.so.0",
    "librt.so.1",
    "libGL.so.1",
    "libOpenGL.so.0",
    "ld-linux-x86-64.so.2",
    "ld-linux-aarch64.so.1",
]

ANDROID_ALLOWED = [
    "libc.so",
    "libm.so",
    "libdl.so",
    "liblog.so",
    "libandroid.so",
    "libEGL.so",
    "libGLESv1_CM.so",
    "libGLESv2.so",
    "libGLESv3.so",
    "libOpenSLES.so",
    "libaaudio.so",
]


# ---- PE ---------------------------------------------------------------------


def pe_imports(data):
    """DLL names from the import and delay-import directories."""
    (pe_offset,) = struct.unpack_from("<I", data, 0x3C)
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError("not a PE file")

    coff = pe_offset + 4
    (num_sections,) = struct.unpack_from("<H", data, coff + 2)
    (opt_size,) = struct.unpack_from("<H", data, coff + 16)
    opt = coff + 20
    (magic,) = struct.unpack_from("<H", data, opt)
    dirs = opt + (96 if magic == 0x10B else 112)

    sections = []
    for i in range(num_sections):
        sec = opt + opt_size + i * 40
        vsize, vaddr, raw_size, raw_ptr = struct.unpack_from("<IIII", data, sec + 8)
        sections.append((vaddr, max(vsize, raw_size), raw_ptr))

    def rva_to_offset(rva):
        for vaddr, size, raw_ptr in sections:
            if vaddr <= rva < vaddr + size:
                return rva - vaddr + raw_ptr
        raise ValueError(f"RVA {rva:#x} is outside every section")

    def c_string(rva):
        start = rva_to_offset(rva)
        return data[start : data.index(b"\0", start)].decode("ascii", "replace")

    names = []

    import_rva, _ = struct.unpack_from("<II", data, dirs + 1 * 8)
    if import_rva:
        entry = rva_to_offset(import_rva)
        while True:
            fields = struct.unpack_from("<IIIII", data, entry)
            if not any(fields):
                break
            names.append(c_string(fields[3]))
            entry += 20

    delay_rva, _ = struct.unpack_from("<II", data, dirs + 13 * 8)
    if delay_rva:
        entry = rva_to_offset(delay_rva)
        while True:
            fields = struct.unpack_from("<8I", data, entry)
            if not any(fields):
                break
            names.append(c_string(fields[1]))
            entry += 32

    return names


def check_pe(path, data):
    names = pe_imports(data)
    bad = [n for n in names if any(fnmatch.fnmatch(n.lower(), p) for p in PE_FORBIDDEN)]

    print(f"{path}: imports {', '.join(sorted(set(names), key=str.lower))}")
    for name in bad:
        print(f"ERROR: {path} needs {name}, which is not part of Windows", file=sys.stderr)
    return not bad


# ---- ELF --------------------------------------------------------------------


def elf_needed(data):
    """DT_NEEDED entries and required symbol versions (e.g. GLIBC_2.34)."""
    if data[:4] != b"\x7fELF":
        raise ValueError("not an ELF file")

    is64 = data[4] == 2
    end = "<" if data[5] == 1 else ">"

    if is64:
        shoff, = struct.unpack_from(end + "Q", data, 0x28)
        shentsize, shnum = struct.unpack_from(end + "HH", data, 0x3A)
        sec_fmt = end + "IIQQQQIIQQ"
        dyn_fmt = end + "qQ"
    else:
        shoff, = struct.unpack_from(end + "I", data, 0x20)
        shentsize, shnum = struct.unpack_from(end + "HH", data, 0x2E)
        sec_fmt = end + "IIIIIIIIII"
        dyn_fmt = end + "iI"

    sections = [struct.unpack_from(sec_fmt, data, shoff + i * shentsize) for i in range(shnum)]

    def string_at(section_index, offset):
        start = sections[section_index][4] + offset
        return data[start : data.index(b"\0", start)].decode("ascii", "replace")

    needed = []
    versions = []
    dyn_size = struct.calcsize(dyn_fmt)

    for sh_name, sh_type, _flags, _addr, sh_offset, sh_size, sh_link, sh_info, _align, _entsize in sections:
        if sh_type == 6:  # SHT_DYNAMIC
            for pos in range(sh_offset, sh_offset + sh_size, dyn_size):
                tag, value = struct.unpack_from(dyn_fmt, data, pos)
                if tag == 0:
                    break
                if tag == 1:  # DT_NEEDED
                    needed.append(string_at(sh_link, value))
        elif sh_type == 0x6FFFFFFE:  # SHT_GNU_verneed
            pos = sh_offset
            for _ in range(sh_info):
                _ver, cnt, _file, aux, nxt = struct.unpack_from(end + "HHIII", data, pos)
                apos = pos + aux
                for _ in range(cnt):
                    _hash, _flags2, _other, name, anext = struct.unpack_from(end + "IHHII", data, apos)
                    versions.append(string_at(sh_link, name))
                    apos += anext
                pos += nxt

    return needed, versions


def glibc_key(version):
    return tuple(int(p) for p in re.findall(r"\d+", version))


def check_elf(path, data, allowed, label):
    needed, versions = elf_needed(data)
    bad = [n for n in needed if n not in allowed]

    print(f"{path}: needs {', '.join(needed) or '(nothing)'}")
    glibc = sorted({v for v in versions if v.startswith("GLIBC_") and re.search(r"\d", v)}, key=glibc_key)
    if glibc:
        print(f"{path}: newest glibc symbol {glibc[-1]}")

    for name in bad:
        print(f"ERROR: {path} needs {name}, which {label} systems may not have", file=sys.stderr)
    return not bad


# ---- APK --------------------------------------------------------------------


def check_apk(path):
    ok = True
    with zipfile.ZipFile(path) as apk:
        libs = [n for n in apk.namelist() if n.startswith("lib/") and n.endswith(".so")]
        if not libs:
            print(f"ERROR: {path} has no native libraries", file=sys.stderr)
            return False

        for name in libs:
            if Path(name).name != "libmain.so":
                print(f"ERROR: {path} ships {name}; everything belongs in libmain.so", file=sys.stderr)
                ok = False
                continue
            ok = check_elf(f"{path}!{name}", apk.read(name), ANDROID_ALLOWED, "Android") and ok

    return ok


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2

    path = Path(argv[1])
    data = path.read_bytes()

    if data[:2] == b"MZ":
        ok = check_pe(path, data)
    elif data[:4] == b"\x7fELF":
        ok = check_elf(path, data, LINUX_ALLOWED, "Linux")
    elif data[:4] == b"PK\x03\x04":
        ok = check_apk(path)
    else:
        print(f"ERROR: {path}: not a PE, ELF or APK file", file=sys.stderr)
        return 2

    if ok:
        print(f"{path}: standalone")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
