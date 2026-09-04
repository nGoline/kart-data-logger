/*
 * dashshot — render the real dashboard to a PNG, on this machine.
 *
 * Compiles lib/DashV2/ui_dash2.c and the generated lib/DashFonts faces against
 * LVGL's software renderer with no display driver at all, draws into a 480x320
 * RGB565 buffer and writes it out. It is the firmware's own layout code and the
 * firmware's own fonts, so what comes out is what the panel will show: glyph
 * metrics, flex layout, alignment offsets and overflow all behave as they do on
 * the device. That is the whole point — a hand-written HTML mock would agree
 * with my arithmetic about widths rather than with LVGL.
 *
 * Known gap: ui_dash2 deliberately does not build the status bar (the firmware
 * keeps a persistent one on lv_layer_top, see the LOCAL notes in ui_dash2.h), so
 * the top 26 px are empty here and the REC/DEMO cell does not appear. Geometry
 * below it is unaffected.
 *
 *   ./dashshot [outdir]     writes one PNG per scene
 */
#include "lvgl.h"
#include "ui_dash2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* From ui_dash2.h, so the harness cannot render at a size the firmware
 * does not use. */
#define W DASH2_SCR_W
#define H DASH2_SCR_H

static uint8_t  s_fb[W * H * 2];          /* RGB565, the display's own format */

/* ---------------------------------------------------------------- PNG out --
 * Written by hand with stored (uncompressed) deflate blocks so this needs no
 * zlib and no image library: a preview tool that cannot be built is no use.
 * ------------------------------------------------------------------------- */
static uint32_t crc_table[256];
static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[n] = c;
    }
}
static uint32_t crc32_buf(const uint8_t *p, size_t n, uint32_t c) {
    c ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
static void be32(uint8_t *o, uint32_t v) {
    o[0] = (uint8_t)(v >> 24); o[1] = (uint8_t)(v >> 16);
    o[2] = (uint8_t)(v >> 8);  o[3] = (uint8_t)v;
}
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[4];
    be32(hdr, len);
    fwrite(hdr, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t c = crc32_buf((const uint8_t *)type, 4, 0);
    if (len) c = crc32_buf(data, len, c);
    uint8_t cb[4]; be32(cb, c);
    fwrite(cb, 1, 4, f);
}

static int write_png_buf(const char *path, const uint8_t *fb, int rows) {
    /* Raw scanlines: a zero filter byte then RGB triples. */
    size_t rawLen = (size_t)rows * (1 + (size_t)W * 3);
    uint8_t *raw = malloc(rawLen);
    if (!raw) return -1;

    size_t o = 0;
    for (int y = 0; y < rows; y++) {
        raw[o++] = 0;
        for (int x = 0; x < W; x++) {
            const uint8_t *px = &fb[((size_t)y * W + x) * 2];
            uint16_t v = (uint16_t)(px[0] | (px[1] << 8));      /* RGB565 LE */
            uint8_t r = (uint8_t)((v >> 11) & 0x1F);
            uint8_t g = (uint8_t)((v >> 5)  & 0x3F);
            uint8_t b = (uint8_t)( v        & 0x1F);
            /* Replicate the high bits into the low ones so full-scale stays
               full-scale rather than 248/252. */
            raw[o++] = (uint8_t)((r << 3) | (r >> 2));
            raw[o++] = (uint8_t)((g << 2) | (g >> 4));
            raw[o++] = (uint8_t)((b << 3) | (b >> 2));
        }
    }

    /* zlib stream: 2-byte header, stored deflate blocks, adler32. */
    size_t maxBlocks = rawLen / 65535 + 1;
    size_t zLen = 2 + rawLen + maxBlocks * 5 + 4;
    uint8_t *z = malloc(zLen);
    if (!z) { free(raw); return -1; }
    size_t zo = 0;
    z[zo++] = 0x78; z[zo++] = 0x01;
    size_t left = rawLen, off = 0;
    do {
        uint16_t n = (uint16_t)(left > 65535 ? 65535 : left);
        int last = (left - n) == 0;
        z[zo++] = (uint8_t)(last ? 1 : 0);
        z[zo++] = (uint8_t)(n & 0xFF);        z[zo++] = (uint8_t)(n >> 8);
        z[zo++] = (uint8_t)(~n & 0xFF);       z[zo++] = (uint8_t)((~n >> 8) & 0xFF);
        memcpy(z + zo, raw + off, n);
        zo += n; off += n; left -= n;
    } while (left);
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < rawLen; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be32(z + zo, (b << 16) | a); zo += 4;

    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return -1; }
    static const uint8_t sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, W); be32(ihdr + 4, (uint32_t)rows);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;  /* 8-bit RGB */
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)zo);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw); free(z);
    return 0;
}

/* ------------------------------------------------------------------ LVGL -- */
static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px) {
    (void)area; (void)px;
    lv_display_flush_ready(d);
}

static lv_display_t *s_disp;

static void shoot(const char *dir, const char *name) {
    /* A few handler passes so flex layout and any pending invalidation settle,
     * then force a full redraw into the buffer. */
    for (int i = 0; i < 4; i++) { lv_tick_inc(16); lv_timer_handler(); }
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(s_disp);

#if LV_COLOR_16_SWAP
    /* lv_conf.h sets LV_COLOR_16_SWAP=1 because the esp_lcd backend needs it, so
     * what LVGL just rendered is byte-swapped RGB565. DisplayGFX.cpp undoes the
     * same swap in its flush callback; this does it for the same reason, and is
     * guarded the same way so the tool stays correct if that setting changes.
     * Getting it wrong is not subtle but it is misleading: the near-black
     * background renders as dark olive and every colour looks like a palette bug. */
    lv_draw_sw_rgb565_swap(s_fb, W * H);
#endif

    char path[512];
    snprintf(path, sizeof path, "%s/dash_%s.png", dir, name);
    if (write_png_buf(path, s_fb, H) == 0) printf("  %s\n", path);
    else                                   printf("  FAILED to write %s\n", path);
}

/* m:ss.mmm, matching lib/UiHelper's dashFmtTime: ui_dash2.c's widths are
 * budgeted for this form. */
static void fmt_ms(char *out, size_t n, uint32_t ms) {
    snprintf(out, n, "%u:%02u.%03u", ms / 60000, (ms % 60000) / 1000, ms % 1000);
}

/* One scene: the figures the firmware would be holding at that moment. The
 * reference lap IS the best lap, so there is no separate field for it. */
typedef struct {
    const char *name;
    int      lap, speed;
    uint32_t clock_ms, best_ms, last_ms, run_ms;
    float    delta_s, split_s;
    dash2_delta_state_t dstate;
    bool     has_ref;
    dash2_sector_state_t ss[3];
    float    sd[3];
} scene_t;

static void play(const char *dir, const scene_t *sc) {
    char b[24];

    ui_dash2_set_speed(sc->speed);

    if (sc->last_ms) {
        char bb[24];
        fmt_ms(b,  sizeof b,  sc->last_ms);
        fmt_ms(bb, sizeof bb, sc->best_ms);
        ui_dash2_set_lap(sc->lap, b, bb);
    } else {
        ui_dash2_set_lap(sc->lap, "", "");
    }

    ui_dash2_set_delta(sc->delta_s, sc->dstate, sc->has_ref);
    ui_dash2_set_delta_bar(sc->split_s, sc->has_ref);

    if (sc->has_ref) {
        uint32_t pred = (uint32_t)((int32_t)sc->best_ms + (int32_t)(sc->delta_s * 1000.0f));
        fmt_ms(b, sizeof b, pred);
        ui_dash2_set_predicted(b);
    }

    fmt_ms(b, sizeof b, sc->clock_ms);
    ui_dash2_set_lap_clock(b);

    for (int k = 0; k < 3; k++)
        ui_dash2_set_sector((dash2_sector_t)k, sc->ss[k], sc->sd[k]);
    if (sc->run_ms) {
        /* Zero-padded, as lib/UiHelper sends it: the Barlow faces have no
         * space glyph, so a leading zero is the only fixed width under ten
         * seconds. */
        snprintf(b, sizeof b, "%02u.%01u", sc->run_ms / 1000, (sc->run_ms % 1000) / 100);
        ui_dash2_set_running_split(b);
    }

    shoot(dir, sc->name);
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : ".";
    crc_init();

    lv_init();
    s_disp = lv_display_create(W, H);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_disp, s_fb, NULL, sizeof s_fb,
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(s_disp, flush_cb);

    ui_dash2_init(NULL);
    ui_dash2_set_mode(DASH2_MODE_NIGHT);

    printf("rendering %dx%d scenes (no status bar — see the note at the top)\n", W, H);

    static const scene_t scenes[] = {
        /* Session up, no reference lap yet: the panel says so in words rather
         * than drawing placeholder digits in a digits-only face. */
        { "1_cold", 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, DASH2_DELTA_NONE, false,
          { DASH2_SECTOR_PENDING, DASH2_SECTOR_PENDING, DASH2_SECTOR_PENDING }, { 0, 0, 0 } },

        /* Mid-lap, up on the best. Bar right of centre. */
        { "2_up", 4, 63, 24180, 62628, 62807, 12300, -0.42f, -0.31f,
          DASH2_DELTA_FASTER, true,
          { DASH2_SECTOR_FASTER, DASH2_SECTOR_ACTIVE, DASH2_SECTOR_PENDING }, { -0.18f, 0, 0 } },

        /* Down on the best. Bar left of centre. */
        { "3_down", 4, 41, 48905, 62628, 62807, 8400, 1.24f, 0.62f,
          DASH2_DELTA_SLOWER, true,
          { DASH2_SECTOR_FASTER, DASH2_SECTOR_SLOWER, DASH2_SECTOR_ACTIVE }, { -0.18f, 1.42f, 0 } },

        /* The case the old layout could not express: a lap thrown away, but
         * gaining right now. Red panel, green bar right of centre, and the
         * sector that cost it clamped to +9.9+. */
        { "4_recover", 6, 58, 71400, 62628, 78225, 6100, 15.62f, -0.24f,
          DASH2_DELTA_SLOWER, true,
          { DASH2_SECTOR_SLOWER, DASH2_SECTOR_FASTER, DASH2_SECTOR_ACTIVE }, { 15.80f, -0.19f, 0 } },

        /* Purple at the line, on the lap that just became the session best. */
        { "5_best", 5, 58, 320, 62140, 62140, 0, -0.488f, -0.488f,
          DASH2_DELTA_BEST, true,
          { DASH2_SECTOR_FASTER, DASH2_SECTOR_FASTER, DASH2_SECTOR_FASTER },
          { -0.18f, -0.12f, -0.19f } },

        /* The first timed lap: S1 and S2 closed but with no best to compare
         * against, so they show their split TIMES rather than blanking. This is
         * the case that made the rail useless for a whole lap. */
        { "7_lap1", 1, 57, 34120, 0, 0, 9400, 0.0f, 0.0f, DASH2_DELTA_NONE, false,
          { DASH2_SECTOR_TIMED, DASH2_SECTOR_TIMED, DASH2_SECTOR_ACTIVE },
          { 11.8f, 12.4f, 0 } },

        /* Widest strings every box can hold, for overflow. Sign and state agree
         * so the shot does not look like a bug in the dash. */
        { "6_widest", 18, 112, 528888, 82444, 88888, 88800, 88.88f, 88.88f,
          DASH2_DELTA_SLOWER, true,
          { DASH2_SECTOR_SLOWER, DASH2_SECTOR_SLOWER, DASH2_SECTOR_ACTIVE },
          { 88.88f, -88.88f, 0 } },
        /* Session stopped / demo left, reached AFTER a scene with a live sector,
         * so this renders the transition rather than a cold start. Everything
         * must be blank: an amber cell holding a stale running split here is the
         * bug where the diff cache and the widget had drifted apart. */
        { "9_stopped", 0, 0, 0, 0, 0, 0, 0.0f, 0.0f, DASH2_DELTA_NONE, false,
          { DASH2_SECTOR_PENDING, DASH2_SECTOR_PENDING, DASH2_SECTOR_PENDING }, { 0, 0, 0 } },
    };

    for (size_t i = 0; i < sizeof scenes / sizeof scenes[0]; i++) play(dir, &scenes[i]);

    /* The day palette, so the second theme is not left untested. Same scene as
     * 2_up under a different name, rather than renaming the file afterwards. */
    scene_t day = scenes[1];
    day.name = "8_day";
    ui_dash2_set_mode(DASH2_MODE_DAY);
    play(dir, &day);
    ui_dash2_set_mode(DASH2_MODE_NIGHT);

    printf("done\n");
    return 0;
}
