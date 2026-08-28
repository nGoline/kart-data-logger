# dashshot — see the dashboard before you flash it

Renders the dashboard to PNG on your machine, so a layout change can be looked
at in a second or two instead of a two-minute build, a flash, and a squint at
the panel.

```sh
cd tools/dashshot
make shots        # -> out/dash_*.png
```

## Why this and not a mock

It compiles the **real** `lib/DashV2/ui_dash2.c` and the **real** generated
`lib/DashFonts` faces against LVGL's software renderer, with no display driver:
LVGL draws into a 480x320 RGB565 buffer and the tool writes that buffer out. So
glyph metrics, flex layout, alignment offsets and overflow all behave exactly as
they do on the device.

That distinction is the entire point. A hand-written HTML mock would agree with
whatever arithmetic you did about label widths; this disagrees with you when you
are wrong. The `5_widest` scene exists for precisely that — it renders the widest
string the lap box can ever hold (`8:88.888`, all wide digits) so "it fits" is an
observation rather than a calculation.

## Scenes

| file | what it is for |
|---|---|
| `1_cold` | session up, no reference lap: the blank delta and muted lap box |
| `2_faster` | mid-lap, up on the best, one sector closed |
| `3_slower` | down on the best, two sectors closed |
| `4_best` | purple SESSION BEST at the line |
| `5_widest` | the widest strings every box can hold, for overflow |
| `6_day` | the day palette, so the second theme is not left untested |

Add one by copying a block in `dashshot.c` — it is a straight sequence of calls
to `ui_dash2`'s public API followed by `settle_and_shoot()`.

## Two things it cannot show

**No status bar.** `ui_dash2` deliberately does not build one: the firmware keeps
a persistent bar on `lv_layer_top()` so it survives across screens, and it is
assembled in `lib/UiHelper` from SquareLine's export. So the top 26 px are empty
here, and the REC / DEMO cell, the battery, GPS, CAM and WIFI cells do not
appear. Geometry below it is unaffected — upstream's bar is 26 px at y=0 too, so
everything else lands where it does on the device.

**Byte order matters.** `lv_conf.h` sets `LV_COLOR_16_SWAP=1` because the esp_lcd
backend needs it, so LVGL renders byte-swapped RGB565 and `DisplayGFX.cpp` undoes
the swap in its flush callback. `dashshot.c` does the same, guarded the same way.
Getting it wrong is not subtle but it is misleading: the near-black background
comes out dark olive and every colour looks like a palette bug.

## Build notes

LVGL comes from the PlatformIO dependency tree
(`.pio/libdeps/display_gfx/lvgl`), so run a firmware build at least once first.
Its objects are cached under `build/`, so the first `make` takes a minute and
every one after it takes about a second. `make clean-local` rebuilds just the
dash and the tool; `make clean` drops the LVGL objects too.
