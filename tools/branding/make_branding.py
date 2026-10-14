#!/usr/bin/env python3
"""Builds the PowerSlide branding assets from the logo.

    python3 tools/branding/make_branding.py [--import <logo.png>]

--import copies a logo into docs/images/powerslide-logo.png, cropped to the
letters; a logo without transparency has its white background removed. Then,
from that file:

  platform/native_branding_data.h   menu logo and window icon, embedded in the game
  platform/windows/powerslide.ico   Windows executable icon

Needs Pillow (pip install pillow). Run it again after changing the logo and
commit the outputs; the build does not run it.
"""

import argparse
import os

from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
LOGO_PATH = os.path.join(ROOT, "docs", "images", "powerslide-logo.png")
HEADER_PATH = os.path.join(ROOT, "platform", "native_branding_data.h")
ICO_PATH = os.path.join(ROOT, "platform", "windows", "powerslide.ico")

# Menu logo height in pixels; the menu draws it at up to about this size.
MENU_LOGO_HEIGHT = 112
ICON_SIZE = 128
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


def import_logo(image):
    """Crops to the letters; colour-to-alpha against white first if the image is opaque."""
    if image.mode in ("RGBA", "LA", "PA") and image.getchannel("A").getextrema()[0] < 255:
        image = image.convert("RGBA")
        return image.crop(image.getbbox())

    image = image.convert("RGB")
    out = Image.new("RGBA", image.size)
    src = image.load()
    dst = out.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b = src[x, y]
            alpha = max(255 - r, 255 - g, 255 - b)
            if alpha == 0:
                dst[x, y] = (0, 0, 0, 0)
                continue
            a = alpha / 255.0

            def unmix(c):
                return max(0, min(255, round((c - 255 * (1 - a)) / a)))

            dst[x, y] = (unmix(r), unmix(g), unmix(b), alpha)
    return out.crop(out.getbbox())


def glyph_columns(image):
    """Column ranges of the separate letters (runs of columns with any ink)."""
    alpha = image.getchannel("A").load()
    inked = [any(alpha[x, y] > 16 for y in range(image.height)) for x in range(image.width)]
    ranges = []
    start = None
    for x, ink in enumerate(inked + [False]):
        if ink and start is None:
            start = x
        elif not ink and start is not None:
            ranges.append((start, x))
            start = None
    return ranges


def make_icon(logo, size):
    """The "P" and "S" of the wordmark side by side, centred on a square."""
    glyphs = glyph_columns(logo)
    if len(glyphs) < 6:
        raise SystemExit("expected the 10 letters of PowerSlide, found %d" % len(glyphs))
    p = logo.crop((glyphs[0][0], 0, glyphs[0][1], logo.height))
    s = logo.crop((glyphs[5][0], 0, glyphs[5][1], logo.height))
    gap = glyphs[1][0] - glyphs[0][1]
    pair = Image.new("RGBA", (p.width + gap + s.width, logo.height))
    pair.paste(p, (0, 0))
    pair.paste(s, (p.width + gap, 0))
    pair = pair.crop(pair.getbbox())

    inner = size * 0.94
    scale = min(inner / pair.width, inner / pair.height)
    pair = pair.resize((max(1, round(pair.width * scale)), max(1, round(pair.height * scale))), Image.LANCZOS)
    icon = Image.new("RGBA", (size, size))
    icon.paste(pair, ((size - pair.width) // 2, (size - pair.height) // 2))
    return icon


def brand_palette(image):
    """The logo's colours: the four most common fully opaque ones."""
    counts = {}
    for r, g, b, a in image.convert("RGBA").get_flattened_data():
        if a == 255:
            counts[(r, g, b)] = counts.get((r, g, b), 0) + 1
    palette = sorted(counts, key=counts.get, reverse=True)[:4]
    if len(palette) < 4:
        raise SystemExit("expected at least 4 colours in the logo, found %d" % len(palette))
    return palette


def rle_indexed(image, palette):
    """Each pixel as one byte, palette index << 6 | alpha >> 2, run-length encoded
    as (count 1..255, byte) pairs. The logo's letters are flat colours, so this
    loses nothing visible."""
    values = []
    for r, g, b, a in image.convert("RGBA").get_flattened_data():
        if a < 4:
            values.append(0)
            continue
        index = min(range(len(palette)), key=lambda i: sum((c - p) ** 2 for c, p in zip((r, g, b), palette[i])))
        values.append((index << 6) | (a >> 2))
    out = bytearray()
    i = 0
    while i < len(values):
        run = 1
        while i + run < len(values) and run < 255 and values[i + run] == values[i]:
            run += 1
        out += bytes((run, values[i]))
        i += run
    return bytes(out)


def c_array(name, data):
    lines = ["static const unsigned char %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 24):
        lines.append("\t" + ", ".join("0x%02x" % b for b in data[i:i + 24]) + ",")
    lines.append("};")
    return "\n".join(lines)


def write_header(logo, menu_logo, icon):
    palette = brand_palette(logo)
    parts = [
        "// Generated by tools/branding/make_branding.py from docs/images/powerslide-logo.png; do not edit.",
        "// Images are run-length encoded (count 1..255, value) byte pairs; each value is a",
        "// kNativeBrandingPalette index << 6 | alpha >> 2. See native_branding.c.",
        "",
        "#ifndef NATIVE_BRANDING_DATA_H",
        "#define NATIVE_BRANDING_DATA_H",
        "",
        "#define NATIVE_BRANDING_LOGO_WIDTH  %d" % menu_logo.width,
        "#define NATIVE_BRANDING_LOGO_HEIGHT %d" % menu_logo.height,
        "#define NATIVE_BRANDING_ICON_SIZE   %d" % icon.width,
        "",
        "static const unsigned char kNativeBrandingPalette[4][3] = {%s};"
        % ", ".join("{0x%02x, 0x%02x, 0x%02x}" % c for c in palette),
        "",
        c_array("kNativeBrandingLogoRle", rle_indexed(menu_logo, palette)),
        "",
        c_array("kNativeBrandingIconRle", rle_indexed(icon, palette)),
        "",
        "#endif",
        "",
    ]
    with open(HEADER_PATH, "w", newline="\n") as f:
        f.write("\n".join(parts))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--import", dest="source", metavar="PNG", help="logo to import first")
    args = parser.parse_args()

    if args.source:
        os.makedirs(os.path.dirname(LOGO_PATH), exist_ok=True)
        import_logo(Image.open(args.source)).save(LOGO_PATH, optimize=True)
        print("wrote", os.path.relpath(LOGO_PATH, ROOT))

    logo = Image.open(LOGO_PATH).convert("RGBA")

    menu_width = round(logo.width * MENU_LOGO_HEIGHT / logo.height)
    menu_logo = logo.resize((menu_width, MENU_LOGO_HEIGHT), Image.LANCZOS)
    icon = make_icon(logo, ICON_SIZE)
    write_header(logo, menu_logo, icon)
    print("wrote", os.path.relpath(HEADER_PATH, ROOT))

    os.makedirs(os.path.dirname(ICO_PATH), exist_ok=True)
    make_icon(logo, 256).save(ICO_PATH, sizes=[(s, s) for s in ICO_SIZES])
    print("wrote", os.path.relpath(ICO_PATH, ROOT))


if __name__ == "__main__":
    main()
