#!/usr/bin/env python3
"""Give the generated Barlow faces tabular figures, in place.

lv_font_conv emits the font's natural (proportional) advance widths, and Barlow
Condensed's are far from uniform: at 44 px a '1' is 12.9 px against a '4' at
22.4, and '+' is 19.3 against '-' at 14.8. On a dashboard that means every digit
that ticks over reflows everything after it, so a lap clock visibly breathes and
"-10.00" occupies a different footprint from "+02.00".

lv_font_conv has no OpenType feature support, so `tnum` is not reachable. This
does the same job directly on the generated table: every digit and both signs get
the widest digit's advance, and each glyph's ofs_x is nudged to keep it centred
in its new cell. The bitmaps are untouched, so nothing is rescaled or reshaped.

Idempotent — it recomputes from whatever is in the file, so a second run is a
no-op. Re-run it after regenerating any face with lv_font_conv, or the jitter
comes back.

    python3 tools/dashfonts/tabularise.py            # all faces, in place
    python3 tools/dashfonts/tabularise.py --report   # measure only, no writes
"""
import re, sys, pathlib

FONTS = pathlib.Path(__file__).resolve().parents[2] / "lib" / "DashFonts"

# Generation range is 0x2B,0x2D,0x2E,0x30-0x3A, so glyph ids run:
#   1 '+'   2 '-'   3 '.'   4..13 '0'..'9'   14 ':'
SIGN_IDS  = (1, 2)
DIGIT_IDS = tuple(range(4, 14))
ROW = re.compile(
    r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), "
    r"\.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}")

def process(path, write):
    src = path.read_text()
    m = re.search(r"glyph_dsc\[\] = \{(.*?)\n\};", src, re.S)
    if not m:
        print(f"  {path.name}: no glyph_dsc, skipped"); return None
    block = m.group(1)
    rows = list(ROW.finditer(block))
    if len(rows) < 15:
        print(f"  {path.name}: {len(rows)} glyphs, unexpected layout, skipped"); return None

    adv = [int(r.group(2)) for r in rows]
    target = max(adv[i] for i in DIGIT_IDS)

    out, changed = block, 0
    for i in SIGN_IDS + DIGIT_IDS:
        r = rows[i]
        old_adv, box_w, ofs_x = int(r.group(2)), int(r.group(3)), int(r.group(5))
        # Centre the untouched bitmap in the wider cell. adv_w is 1/16 px,
        # ofs_x is whole pixels.
        new_ofs = ofs_x + round((target - old_adv) / 2 / 16)
        if old_adv == target and new_ofs == ofs_x:
            continue
        new_row = (f"{{.bitmap_index = {r.group(1)}, .adv_w = {target}, "
                   f".box_w = {box_w}, .box_h = {r.group(4)}, "
                   f".ofs_x = {new_ofs}, .ofs_y = {r.group(6)}}}")
        out = out.replace(r.group(0), new_row, 1)
        changed += 1

    px = target / 16.0
    span = (min(adv[i] for i in DIGIT_IDS), max(adv[i] for i in DIGIT_IDS))
    print(f"  {path.name}: digits {span[0]/16:.2f}-{span[1]/16:.2f} px "
          f"-> {px:.2f} px uniform ({changed} glyph(s) rewritten)")
    if write and changed:
        path.write_text(src.replace(block, out, 1))
    return px

def main():
    report = "--report" in sys.argv
    print("tabular figures:" + (" (report only)" if report else ""))
    widths = {}
    for f in sorted(FONTS.glob("font_*.c")):
        px = process(f, not report)
        if px: widths[f.stem] = px
    print("\nlayout constants follow from these:")
    for k, v in widths.items():
        print(f"  {k}: 1 digit {v:.2f} px, 3 digits {v*3:.1f} px, "
              f"m:ss.mmm about {v*6 + 23:.1f} px")

main()
