#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The theme list, in one place: writes firmware/lsdj_theme.h's tables and the web
page's (site/lsdj/fx-model.js) THEMES / PRESETS from it.

    python3 tools/themes.py [--site ../../site]

Every theme is checked as tests/lsdj_theme.c does (the accent stands out from each
shade, white text from the darkest) and a bit more (the lightest shade reads on the
darkest, the name fits "THEME: " + name in LSDj's font). A theme whose accent fails
gets the candidate that stands out best; the shades are put lightest first. Ids are
the list order and are saved in the settings: only ever add at the end."""
import argparse, re, sys
from pathlib import Path

CUSTOM = 7

# (name, [4 shades], accent): RGB hex strings, or RGB565 ints for the first themes
# (kept bit for bit). CUSTOM's slot holds CLASSIC and is never shown.
THEMES = [
    ("CLASSIC", [0xE7DA, 0x9D50, 0x5308, 0x18E3], 0xFD20),
    ("POCKET", [0xFFFF, 0xAD55, 0x52AA, 0x0000], 0xFD20),
    ("SUNSET", [0xFFBA, 0xD42E, 0x786D, 0x2087], 0x07FF),
    ("ICE", [0xDFFF, 0x6E9C, 0x3274, 0x1108], 0xF81F),
    ("BACON", [0xFF9B, 0xF48C, 0xA226, 0x3082], 0x07E0),
    ("VIRTUAL", [0xF800, 0xA800, 0x5000, 0x0000], 0x07FF),
    ("MIDNIGHT", [0xB71F, 0x5C19, 0x29CF, 0x0843], 0xFD20),
    ("CUSTOM", [0xE7DA, 0x9D50, 0x5308, 0x18E3], 0xFD20),
    # the FM-1's own colourways (M-VAVE's product photos)
    ("WHITE GREY", [0xE6FA, 0xBDB5, 0x4229, 0x2966], 0xDA63),
    ("BLACK GREEN", [0x8E99, 0x6D95, 0x4229, 0x2104], 0xDA63),
    ("BLACK", [0x94F4, 0x630C, 0x3186, 0x18E3], 0xDA63),
    ("ORANGE", [0xE369, 0xBA24, 0x6123, 0x18E3], 0x7677),
    ("PURPLE", [0xDD7A, 0xC417, 0x9B56, 0x51ED], 0xDA63),
    ("WHITE BLUE", [0xEF5C, 0xC618, 0x5371, 0x320A], 0xDA63),
    ("WHITE GREEN", [0xEF5C, 0xAEB9, 0x7657, 0x4B8D], 0xDA63),
    # handhelds and old screens
    ("DMG", ["9BBC0F", "8BAC0F", "306230", "0F380F"], "FF5050"),
    ("GB LIGHT", ["7FF0D8", "40C8A8", "207860", "0C3028"], "FFA020"),
    ("SGB SEPIA", ["F8E8C8", "D8A878", "905830", "301808"], "40A0FF"),
    ("MATRIX", ["80FF80", "30C030", "106010", "000800"], "FFFFFF"),
    ("AMBER CRT", ["FFC060", "D08020", "704000", "1A0C00"], "40C0FF"),
    ("C64", ["A0A0FF", "7070E0", "40318D", "202060"], "FFFF60"),
    ("CGA", ["FFFFFF", "55FFFF", "FF55FF", "000000"], "FFFF55"),
    ("TERMINAL", ["FFFFFF", "AAAAAA", "555555", "000000"], "00FF00"),
    ("HOT DOG", ["FFFF00", "FF0000", "800000", "000000"], "FFFFFF"),
    ("BLUEPRINT", ["FFFFFF", "9CC4FF", "2A5DB0", "0B2A66"], "FFD000"),
    ("PAPER", ["FFFFF0", "C8C8B8", "707068", "202020"], "E03030"),
    ("INK", ["F0F0FF", "8090C0", "303060", "0A0A1A"], "FFC040"),
    # editor colour schemes
    ("NORD", ["ECEFF4", "88C0D0", "4C566A", "2E3440"], "D08770"),
    ("DRACULA", ["F8F8F2", "BD93F9", "6272A4", "282A36"], "50FA7B"),
    ("SOLARIZED", ["FDF6E3", "93A1A1", "586E75", "002B36"], "CB4B16"),
    ("MONOKAI", ["F8F8F2", "A6E22E", "75715E", "272822"], "F92672"),
    ("GRUVBOX", ["FBF1C7", "D79921", "665C54", "282828"], "83A598"),
    # neon and night
    ("VAPORWAVE", ["FFD6F6", "FF71CE", "01CDFE", "1A0A2E"], "FFFB96"),
    ("SYNTHWAVE", ["F9C80E", "FF4365", "540D6E", "1A0933"], "00F0FF"),
    ("CYBERPUNK", ["FCEE0A", "00F0FF", "711C91", "0D0221"], "FF2A6D"),
    ("MIAMI", ["FFFFFF", "4CC9F0", "F72585", "1A0B2E"], "FFD000"),
    ("NEON", ["39FF14", "FF10F0", "1010FF", "000000"], "FFFF00"),
    ("NIGHT DRIVE", ["FFE0B0", "FF9040", "4040A0", "0A0A20"], "40FFD0"),
    ("PLASMA", ["FFD0FF", "E060FF", "7020C0", "200040"], "40FF90"),
    ("TOXIC", ["E0FF40", "A0E000", "406000", "101800"], "FF30FF"),
    ("GALAXY", ["E0D8FF", "9080E0", "403080", "0C0820"], "FFE060"),
    ("AURORA", ["D0FFE8", "60E0B0", "6040A0", "0A1028"], "FF60C0"),
    ("OBSIDIAN", ["B0B0C8", "606078", "282838", "08080C"], "C060FF"),
    ("SHADOW", ["C0C0C0", "808080", "404040", "101010"], "FF4040"),
    # nature
    ("OCEAN", ["CAF0F8", "48CAE4", "0077B6", "03045E"], "FFB703"),
    ("FOREST", ["D8F3DC", "74C69D", "2D6A4F", "081C15"], "F4A261"),
    ("LAVA", ["FFE66D", "FF6B35", "9E2A2B", "1A0A0A"], "3DDCFF"),
    ("VOLCANO", ["FFD040", "FF5010", "801000", "180000"], "40E0FF"),
    ("DESERT", ["F8E8C0", "E0B070", "A06030", "402010"], "40B0C0"),
    ("ARCTIC", ["FFFFFF", "C8E0F0", "6090B0", "203040"], "FF6060"),
    ("SKY", ["F0F8FF", "90C8F0", "4080C0", "102040"], "FFA040"),
    ("SEAFOAM", ["E0FFF8", "80E0C8", "308878", "0C2A26"], "FF7050"),
    ("SWAMP", ["D8E0A0", "90A050", "485828", "141A0C"], "E070FF"),
    ("SAKURA", ["FFF0F5", "F7B7C8", "B05A7A", "3B1F2B"], "7CD957"),
    ("OLIVE", ["E8E8C8", "B0B070", "606830", "1C2010"], "E06040"),
    ("TEA", ["F0F8E0", "B8D098", "6C8458", "1C2418"], "E06080"),
    # space
    ("MOON", ["F0F0F0", "B8B8C0", "606070", "18181C"], "FFD040"),
    ("SUN", ["FFFFE0", "FFE060", "E08000", "402000"], "4080FF"),
    ("MARS", ["FFD8C0", "E07850", "803020", "200A06"], "60D0FF"),
    ("NEPTUNE", ["D8E8FF", "6090F0", "2040A0", "080C30"], "FFC040"),
    # gems and metals
    ("GOLD", ["FFF0B0", "E0B030", "806010", "201804"], "40A0FF"),
    ("STEEL", ["E0E4E8", "A0A8B0", "506070", "182028"], "FF8020"),
    ("RUST", ["F0D0B0", "C07040", "703018", "201008"], "40C0E0"),
    ("EMERALD", ["D0FFE8", "40D090", "107048", "041C12"], "FF60A0"),
    ("RUBY", ["FFD0D8", "E04060", "801028", "200408"], "40E0FF"),
    ("SAPPHIRE", ["D0E0FF", "4070E0", "103090", "040A28"], "FFD040"),
    ("AMETHYST", ["F0D8FF", "B070E0", "602090", "180828"], "80FF60"),
    ("TOPAZ", ["FFF0C8", "FFC040", "B07010", "2C1C04"], "4060FF"),
    ("PEARL", ["FFFFF8", "E8E4D8", "A8A090", "383430"], "40A0E0"),
    ("ROSE GOLD", ["FFE8E0", "E8B0A0", "A06860", "301C1C"], "60D0D0"),
    ("CORAL", ["FFE8E0", "FF8070", "C04048", "301014"], "30C0B0"),
    # sweets and fruit
    ("BUBBLEGUM", ["FFE4F2", "FF9ACB", "C8508A", "3A1030"], "30D0FF"),
    ("CANDY", ["FFF0F8", "FFA8D0", "A060E0", "2A1050"], "60F0E0"),
    ("COTTON", ["FFFFFF", "FFD0E8", "B0D8FF", "303050"], "FF8040"),
    ("PEACH", ["FFF0E0", "FFB890", "C06850", "3A1A14"], "4090FF"),
    ("MINT", ["E8FFF4", "9EE6C8", "3E8F74", "10302A"], "FF5C8A"),
    ("BANANA", ["FFF8B0", "F0D040", "907020", "302008"], "6060FF"),
    ("GRAPE", ["E8D8FF", "A070E0", "5A2A8A", "1E0A33"], "9CFF57"),
    ("CHERRY", ["FFE0E0", "FF5060", "A01028", "300008"], "40E0C0"),
    ("LIME", ["F0FFD0", "B0F040", "508010", "142008"], "FF40A0"),
    ("BERRY", ["F8E0F0", "D070B0", "802060", "200818"], "60E0A0"),
    ("COFFEE", ["F3E9DC", "C08552", "5E3023", "1E0F0A"], "7FD8FF"),
    # the burrito and its friends
    ("BURRITO", ["FFF4D6", "F2C14E", "A0522D", "2B1B10"], "5DBB63"),
    ("SALSA", ["FFE8D0", "FF6040", "2E8B3E", "1A0A08"], "FFE000"),
    ("GUAC", ["F0FFD8", "A8D86C", "4A7A2A", "141E0A"], "FF5040"),
    ("TACO", ["FFF0C0", "E8B040", "8B4513", "241008"], "40C060"),
    ("HORCHATA", ["FFF8F0", "E8D8C0", "A08868", "302418"], "D06030"),
    # holidays and the rest
    ("HALLOWEEN", ["FFD8A0", "FF8C00", "6A1B9A", "120812"], "7CFF00"),
    ("XMAS", ["F8FFF8", "E04040", "207040", "081808"], "FFD700"),
    ("KIRBY", ["FFE8F0", "FFA0C0", "D04070", "401020"], "40A0FF"),
    ("SLIME", ["E8FFE0", "90F070", "309040", "0A2010"], "FF60FF"),
    ("DENIM", ["E0E8F0", "8098B8", "3A5070", "101828"], "F0B040"),
]

ACCENT_CANDIDATES = ["FD20", "07FF", "F81F", "07E0", "FFE0", "F800", "FFFF", "DA63", "7677", "041F"]


def to565(c):
    if isinstance(c, int):
        return c
    r, g, b = int(c[0:2], 16), int(c[2:4], 16), int(c[4:6], 16)
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def dist(a, b):   # lsdj_theme_dist
    f = lambda c: ((c >> 11) << 3, ((c >> 5) & 63) << 2, (c & 31) << 3)
    return sum(abs(x - y) for x, y in zip(f(a), f(b)))


def lum(c):   # tests/lsdj_theme.c's light-to-dark order
    return (c >> 11) + ((c >> 5) & 63) // 2 + (c & 31)


FONT = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -#?!.:=+/")


def build():
    out, notes = [], []
    for i, (name, shades, accent) in enumerate(THEMES):
        assert len(name) <= 12 and set(name) <= FONT, name                 # "THEME: " + name <= 19
        gb = sorted((to565(c) for c in shades), key=lum, reverse=True) if i > 14 else [to565(c) for c in shades]
        acc = to565(accent)
        ok = lambda a: min(dist(a, s) for s in gb) >= 150
        if not ok(acc):
            best = max((int(c, 16) for c in ACCENT_CANDIDATES), key=lambda a: min(dist(a, s) for s in gb))
            notes.append(f"{name}: accent {acc:04X} too close to a shade, using {best:04X}")
            acc = best
        assert ok(acc), name
        if dist(0xFFFF, gb[3]) < 400: notes.append(f"DARK {name}: {dist(0xFFFF, gb[3])}")
        if i > 14 and dist(gb[0], gb[3]) < 250: notes.append(f"FAINT {name}: {dist(gb[0], gb[3])}")
        out.append((name, gb, acc))
    names = [n for n, _, _ in out]
    assert len(set(names)) == len(names) and names[CUSTOM] == "CUSTOM" and len(out) <= 128
    return out, notes


def c_tables(out):
    n = len(out)
    L = [f"#define LSDJ_THEMES {n}u         /* presets and CUSTOM; generated by tools/themes.py: edit it there */",
         f"#define LSDJ_THEME_PRESETS {n}u  /* lsdj_themes[] entries; CUSTOM's slot holds CLASSIC and is never used */",
         "#define LSDJ_THEME_CUSTOM 7u    /* kept at 7: saved settings from before the FM-1 themes still mean CUSTOM */",
         "static const char *const lsdj_theme_name[LSDJ_THEMES] = {"]
    for k in range(0, n, 8):
        L.append("    " + ", ".join(f'"{nm}"' for nm, _, _ in out[k:k + 8]) + ",")
    L.append("};")
    L.append("static const lsdj_theme_t lsdj_themes[LSDJ_THEME_PRESETS] = {")
    for nm, gb, acc in out:
        L.append(f"    {{{{0x{gb[0]:04X}, 0x{gb[1]:04X}, 0x{gb[2]:04X}, 0x{gb[3]:04X}}}, 0xFFFF, 0x{gb[3]:04X}, 0x{acc:04X}, 0x4208}},   /* {nm} */")
    L.append("};")
    return "\n".join(L)


def js_tables(out):
    title = lambda s: " ".join(w[:1] + w[1:].lower() for w in s.split(" "))
    L = ["export const THEMES = ["]
    for k in range(0, len(out), 8):
        L.append("  " + ", ".join(f"'{title(nm)}'" for nm, _, _ in out[k:k + 8]) + ",")
    L.append("];   // generated by tools/themes.py (firmware repo): edit it there")
    L.append("export const CUSTOM_THEME = 7;")
    L.append("export const PRESETS = [   // gb[4] (lightest first), text, dark, accent, empty bar")
    for i, (nm, gb, acc) in enumerate(out):
        L.append("  null,   // 7: Custom" if i == CUSTOM else
                 f"  [0x{gb[0]:04X}, 0x{gb[1]:04X}, 0x{gb[2]:04X}, 0x{gb[3]:04X}, 0xFFFF, 0x{gb[3]:04X}, 0x{acc:04X}, 0x4208],")
    L.append("];")
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--site", default=None)
    a = ap.parse_args()
    out, notes = build()
    for m in notes:
        print("note:", m)
    if any(m.startswith(("FAINT", "DARK")) for m in notes):
        return 1
    root = Path(__file__).resolve().parent.parent
    h = root / "firmware/lsdj_theme.h"
    s = h.read_text()
    s2 = re.sub(r"#define LSDJ_THEMES .*?static const lsdj_theme_t lsdj_themes\[.*?\n};\n",lambda _: c_tables(out) + "\n", s, count=1, flags=re.S)
    assert s2 != s or c_tables(out) in s
    h.write_text(s2)
    if a.site:
        j = Path(a.site) / "lsdj/fx-model.js"
        t = j.read_text()
        t2 = re.sub(r"export const THEMES = \[.*?(?=export const COLOR_NAMES)", lambda _: js_tables(out) + "\n", t, count=1, flags=re.S)
        assert t2 != t or js_tables(out) in t, "fx-model.js: THEMES / PRESETS not found"
        j.write_text(t2)
    print(f"{len(out)} themes ({len(out) - 1} presets + CUSTOM)")


if __name__ == "__main__":
    sys.exit(main())
