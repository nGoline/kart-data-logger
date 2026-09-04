/* ============================================================================
 * ui_dash2.c — see ui_dash2.h. LVGL 9 · 480x320
 *
 * Layout (px):
 *   0    ─ status bar, 26 px ─ LOCAL: built elsewhere, on lv_layer_top()
 *   34   SPEED caption          128  delta panel, 104 (bar + delta + predicted)
 *   48   speed value            238  band, 78 (lap clock + 3 sector cells)
 *   40   LAP / BEST (right)     316  ─ end
 *
 * The header is upstream's and untouched. Everything below it is re-cut for
 * racing use — see the LOCAL block in ui_dash2.h for what changed and why.
 * ============================================================================ */
#include "ui_dash2.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

LV_FONT_DECLARE(lv_font_montserrat_12)
LV_FONT_DECLARE(lv_font_montserrat_14)
LV_FONT_DECLARE(lv_font_montserrat_16)

#define SCR_W        DASH2_SCR_W
#define SCR_H        DASH2_SCR_H
#define PAD_X         14
#define CONTENT_W    (SCR_W - PAD_X * 2)

/* Three tabular digits at font_speed_72 (36.56 px each) plus a little slack.
 * See tools/dashfonts/tabularise.py for where that figure comes from. */
#define SPEED_VAL_W  114

/* Delta panel: replaces the vs-best hero. Same footprint, but it carries a
 * centre-zero bar as well as the number, because the bar is the only part of
 * this readable without taking your eyes off the track. */
#define PANEL_Y      128
#define PANEL_H      104
#define BAR_INSET     20
#define BAR_W        (CONTENT_W - BAR_INSET * 2)
#define BAR_H         28
#define BAR_Y         12
#define BAR_HALF     (BAR_W / 2)

#define BAND_Y       238
#define BAND_H        78
/* Tabular digits take the widest digit's width, so m:ss.mmm at font_pill_44
 * is about 157 px. 174 leaves it air. */
#define CLOCK_W      174
#define CHIP_GAP       6
#define CHIP_X       (PAD_X + CLOCK_W + CHIP_GAP)
#define CHIP_W       ((CONTENT_W - CLOCK_W - CHIP_GAP * 3) / 3)

typedef struct {
    /* fg2 is a softer white for real data that carries no judgement — a sector
     * split with nothing to compare it against. Brighter than muted, which means
     * "nothing here", and dimmer than fg so it does not compete with the clock. */
    uint32_t bg, fg, fg2, muted, rule, track, accent;
    /* warn is the delta bar run off its scale, so "a second down" and
     * "fifteen seconds down" are not drawn identically. */
    uint32_t good, bad, warn, good_deep, bad_deep;
    /* Purple, for the session's fastest lap. Same shape as the good/bad pairs:
     * a deep fill with a bright border and text on top of it. */
    uint32_t best, best_deep;
} dash2_tokens_t;

static const dash2_tokens_t TOK_NIGHT = {
    .bg = 0x050608, .fg = 0xF6F8FB, .fg2 = 0xCBD0D8, .muted = 0x6B7280, .rule = 0x1C2026,
    .track = 0x1A1D23, .accent = 0xFFD400, .good = 0x2EE07A, .bad = 0xFF3B3B,
    .warn = 0xFFB020, .good_deep = 0x0F3A23, .bad_deep = 0x3A0F10,
    .best = 0xB44DFF, .best_deep = 0x2A0F3A,
};
static const dash2_tokens_t TOK_DAY = {
    .bg = 0x050608, .fg = 0xFFFFFF, .fg2 = 0xD8DEE6, .muted = 0x9AA3AF, .rule = 0x2A2F37,
    .track = 0x20242C, .accent = 0x00E5FF, .good = 0x29FF8A, .bad = 0xFF5050,
    .warn = 0xFFC940, .good_deep = 0x0F3A23, .bad_deep = 0x3A0F10,
    .best = 0xC77DFF, .best_deep = 0x2A0F3A,
};

static dash2_tokens_t T = {0};
static dash2_mode_t   s_mode = DASH2_MODE_NIGHT;

/* The third sector is labelled S3, not END. The gate that closes it is still
 * LAP_GATE_END internally — the finish line doubles as that gate — but from
 * the driver's seat these are simply the three sectors of a lap. */
static const char *SECTOR_TAG[DASH2_SECTOR_COUNT] = { "S1", "S2", "S3" };

typedef struct { dash2_sector_state_t state; float delta; } sector_slot_t;

static sector_slot_t s_sectors[DASH2_SECTOR_COUNT];
static int   s_current   = -1;
static float s_lap_delta   = 0.0f;
static float s_split_delta = 0.0f;
static bool  s_split_has   = false;
static dash2_delta_state_t s_delta_state = DASH2_DELTA_NONE;
static bool  s_delta_has   = false;
static char  s_running[16] = "0.0";

/* Last painted, so a repaint costs only when the colour actually changes:
 * touching the panel invalidates it and all seven children. -1 forces the
 * first paint; apply_theme() resets these because T moved underneath them. */
static int s_painted_state = -1;
static int32_t s_painted_bar_col = -1;

static lv_obj_t *scr;
static lv_obj_t *lbl_speed_cap, *lbl_speed_v, *lbl_speed_unit;
static lv_obj_t *box_lap, *lbl_lap_n, *lbl_lap_v, *lbl_best;
static lv_obj_t *panel_delta, *bar_track, *bar_fill, *bar_mid;
static lv_obj_t *lbl_delta_v, *lbl_pred_cap, *lbl_pred, *lbl_noref;
static lv_obj_t *clock_box, *lbl_clock;
static lv_obj_t *chip[DASH2_SECTOR_COUNT];
static lv_obj_t *chip_tag[DASH2_SECTOR_COUNT], *chip_val[DASH2_SECTOR_COUNT];

static inline lv_color_t C(uint32_t hex) { return lv_color_hex(hex); }

static void refresh_delta(void);
static void refresh_bar(void);
static void refresh_clock_box(void);
static void refresh_rail(void);
static void apply_theme(void);

static lv_obj_t *panel(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static lv_obj_t *caption(lv_obj_t *parent, const char *txt, int track_px)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(l, C(T.muted), 0);
    lv_obj_set_style_text_letter_space(l, track_px, 0);
    return l;
}


void ui_dash2_init(lv_obj_t *parent)
{
    if (!parent) parent = lv_screen_active();
    scr    = parent;
    T      = TOK_NIGHT;
    s_mode = DASH2_MODE_NIGHT;

    for (int i = 0; i < DASH2_SECTOR_COUNT; i++) {
        s_sectors[i].state = DASH2_SECTOR_PENDING;
        s_sectors[i].delta = 0.0f;
    }

    lv_obj_set_size(scr, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(scr, C(T.bg), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* LOCAL: no status bar here — see the header. The 26 px it would occupy is
     * covered by the persistent bar on lv_layer_top(), so every y below is
     * still the mock's value. */

    /* ---------- SPEED (top-left) ---------- */
    lbl_speed_cap = caption(scr, "SPEED", 2);
    lv_obj_set_pos(lbl_speed_cap, PAD_X, 34);

    /* Fixed box, text right-aligned inside it: three tabular digits fit, so the
     * label's width never changes and KM/H stays put. Right-aligned pins the
     * ones digit, which is the one that changes every sample. */
    lbl_speed_v = lv_label_create(scr);
    lv_label_set_text(lbl_speed_v, "0");
    lv_obj_set_style_text_font(lbl_speed_v, DASH2_FONT_SPEED, 0);
    lv_obj_set_style_text_color(lbl_speed_v, C(T.fg), 0);
    lv_obj_set_width(lbl_speed_v, SPEED_VAL_W);
    lv_obj_set_style_text_align(lbl_speed_v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(lbl_speed_v, PAD_X, 48);

    lbl_speed_unit = caption(scr, "KM/H", 2);
    lv_obj_align_to(lbl_speed_unit, lbl_speed_v, LV_ALIGN_OUT_RIGHT_BOTTOM, 7, -8);

    /* ---------- LAP (top-right) ---------- */
    box_lap = panel(scr);
    lv_obj_set_size(box_lap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box_lap, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box_lap, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(box_lap, 9, 0);
    lbl_lap_n = caption(box_lap, "LAP 0", 2);
    lbl_lap_v = lv_label_create(box_lap);
    lv_label_set_text(lbl_lap_v, "0:00.00");
    lv_obj_set_style_text_font(lbl_lap_v, DASH2_FONT_LAP, 0);
    lv_obj_set_style_text_color(lbl_lap_v, C(T.fg), 0);
    lv_obj_align(box_lap, LV_ALIGN_TOP_RIGHT, -PAD_X, 40);

    lbl_best = lv_label_create(scr);
    lv_label_set_text(lbl_best, "BEST --");
    lv_obj_set_style_text_font(lbl_best, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_best, C(T.muted), 0);
    lv_obj_align_to(lbl_best, box_lap, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 3);

    /* ---------- DELTA PANEL — bar + number + predicted lap ---------- */
    panel_delta = panel(scr);
    lv_obj_set_size(panel_delta, CONTENT_W, PANEL_H);
    lv_obj_set_pos(panel_delta, PAD_X, PANEL_Y);
    lv_obj_set_style_radius(panel_delta, 8, 0);
    lv_obj_set_style_bg_opa(panel_delta, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel_delta, 1, 0);

    bar_track = panel(panel_delta);
    lv_obj_set_size(bar_track, BAR_W, BAR_H);
    lv_obj_set_pos(bar_track, BAR_INSET, BAR_Y);
    lv_obj_set_style_radius(bar_track, BAR_H / 2, 0);
    lv_obj_set_style_bg_opa(bar_track, LV_OPA_60, 0);

    bar_fill = panel(bar_track);
    lv_obj_set_style_radius(bar_fill, 3, 0);
    lv_obj_set_style_bg_opa(bar_fill, LV_OPA_COVER, 0);

    /* Centre tick, taller than the track so zero stays findable peripherally. */
    bar_mid = panel(panel_delta);
    lv_obj_set_size(bar_mid, 2, BAR_H + 12);
    lv_obj_set_pos(bar_mid, BAR_INSET + BAR_HALF - 1, BAR_Y - 6);
    lv_obj_set_style_bg_opa(bar_mid, LV_OPA_COVER, 0);

    /* Cumulative lap delta: the precision read, for a straight. */
    lbl_delta_v = lv_label_create(panel_delta);
    lv_label_set_text(lbl_delta_v, "0.00");
    lv_obj_set_style_text_font(lbl_delta_v, DASH2_FONT_PILL, 0);
    lv_obj_align(lbl_delta_v, LV_ALIGN_BOTTOM_LEFT, BAR_INSET, -6);

    /* Predicted lap: where this lap is heading. The caption stays OPTIMAL even
     * through the session-best flash, because the number under it is the
     * predicted lap either way — so it is written once, here. */
    lbl_pred_cap = lv_label_create(panel_delta);
    lv_label_set_text(lbl_pred_cap, "OPTIMAL");
    lv_obj_set_style_text_font(lbl_pred_cap, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_letter_space(lbl_pred_cap, 2, 0);
    lv_obj_set_style_opa(lbl_pred_cap, LV_OPA_70, 0);

    lbl_pred = lv_label_create(panel_delta);
    lv_label_set_text(lbl_pred, "0:00.000");
    lv_obj_set_style_text_font(lbl_pred, DASH2_FONT_LAP, 0);
    lv_obj_align(lbl_pred, LV_ALIGN_BOTTOM_RIGHT, -BAR_INSET, -2);

    /* Aligned once, not per update: lv_obj_align_to() implies a synchronous
     * lv_obj_update_layout() over the whole tree, and m:ss.mmm in tabular
     * digits is always the same width, so the caption never moves. */
    lv_obj_align_to(lbl_pred_cap, lbl_pred, LV_ALIGN_OUT_TOP_RIGHT, 0, -2);

    /* Said in words: the Barlow faces are digits-only, so a blanked "--.--" at
     * this size is a row of thick bars that reads as debris, not absence. */
    lbl_noref = lv_label_create(panel_delta);
    lv_label_set_text(lbl_noref, "NO REFERENCE LAP YET");
    lv_obj_set_style_text_font(lbl_noref, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_letter_space(lbl_noref, 2, 0);
    lv_obj_align(lbl_noref, LV_ALIGN_BOTTOM_MID, 0, -20);

    /* ---------- LAP CLOCK ---------- */
    clock_box = panel(scr);
    lv_obj_set_size(clock_box, CLOCK_W, BAND_H);
    lv_obj_set_pos(clock_box, PAD_X, BAND_Y);
    lv_obj_set_style_radius(clock_box, 7, 0);
    lv_obj_set_style_bg_opa(clock_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(clock_box, 1, 0);

    lbl_clock = lv_label_create(clock_box);
    lv_label_set_text(lbl_clock, "0:00.000");
    lv_obj_set_style_text_font(lbl_clock, DASH2_FONT_PILL, 0);
    lv_obj_align(lbl_clock, LV_ALIGN_CENTER, 0, 2);

    /* ---------- SECTOR CELLS ---------- */
    for (int i = 0; i < DASH2_SECTOR_COUNT; i++) {
        chip[i] = panel(scr);
        lv_obj_set_size(chip[i], CHIP_W, BAND_H);
        lv_obj_set_pos(chip[i], CHIP_X + i * (CHIP_W + CHIP_GAP), BAND_Y);
        lv_obj_set_style_radius(chip[i], 7, 0);
        lv_obj_set_style_bg_opa(chip[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(chip[i], 1, 0);

        chip_tag[i] = lv_label_create(chip[i]);
        lv_label_set_text(chip_tag[i], SECTOR_TAG[i]);
        lv_obj_set_style_text_font(chip_tag[i], &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_letter_space(chip_tag[i], 2, 0);
        lv_obj_align(chip_tag[i], LV_ALIGN_TOP_LEFT, 8, 4);

        chip_val[i] = lv_label_create(chip[i]);
        lv_label_set_text(chip_val[i], "--");
        lv_obj_set_style_text_font(chip_val[i], DASH2_FONT_PILL, 0);
        lv_obj_align(chip_val[i], LV_ALIGN_BOTTOM_MID, 0, -2);
    }

    refresh_delta();
    refresh_bar();
    refresh_clock_box();
    refresh_rail();
    apply_theme();
}

static void paint_delta_surface(lv_obj_t *box, dash2_delta_state_t st,
                                lv_obj_t **texts, int n)
{
    uint32_t fill, line, text;
    switch (st) {
        case DASH2_DELTA_BEST:   fill = T.best_deep; line = text = T.best; break;
        case DASH2_DELTA_SLOWER: fill = T.bad_deep;  line = text = T.bad;  break;
        case DASH2_DELTA_FASTER: fill = T.good_deep; line = text = T.good; break;
        /* Nothing to compare with. Reads as the sector band's own pending
         * state rather than as a delta of zero. */
        default:                 fill = T.track;     line = T.rule; text = T.muted; break;
    }
    lv_obj_set_style_bg_color(box, C(fill), 0);
    lv_obj_set_style_border_color(box, C(line), 0);
    for (int i = 0; i < n; i++)
        if (texts[i]) lv_obj_set_style_text_color(texts[i], C(text), 0);
}

static void refresh_delta(void)
{
    if (!panel_delta) return;

    if (s_delta_has) {
        lv_label_set_text_fmt(lbl_delta_v, "%s%.2f",
                              s_lap_delta < 0.0f ? "-" : "+", fabsf(s_lap_delta));
        lv_obj_remove_flag(lbl_delta_v,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_pred,     LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_pred_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_noref,       LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lbl_delta_v,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_pred,     LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_pred_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(lbl_noref, LV_OBJ_FLAG_HIDDEN);
    }

    if ((int)s_delta_state != s_painted_state) {
        lv_obj_t *texts[] = { lbl_delta_v, lbl_pred, lbl_pred_cap };
        paint_delta_surface(panel_delta, s_delta_state, texts, 3);
        s_painted_state = (int)s_delta_state;
    }
}

/* Split-relative, not cumulative — see ui_dash2_set_delta_bar in the header. */
static void refresh_bar(void)
{
    if (!bar_fill) return;

    if (!s_split_has) { lv_obj_set_size(bar_fill, 0, 0); return; }

    float d = s_split_delta;
    bool pegged = fabsf(d) > DASH2_BAR_FULLSCALE_S;
    if (d >  DASH2_BAR_FULLSCALE_S) d =  DASH2_BAR_FULLSCALE_S;
    if (d < -DASH2_BAR_FULLSCALE_S) d = -DASH2_BAR_FULLSCALE_S;

    /* Gaining grows RIGHT, losing grows LEFT — the opposite of the delta's
     * sign, because faster is a negative number but a rightward bar. The sign
     * is arithmetic; the direction is muscle memory, and this is the one
     * element read without looking, so it follows the hand, not the minus. */
    float grow = -d;
    int w = (int)(fabsf(grow) / DASH2_BAR_FULLSCALE_S * BAR_HALF);
    if (w == 0 && grow != 0.0f) w = 2;
    lv_obj_set_size(bar_fill, w, BAR_H - 8);
    lv_obj_set_pos(bar_fill, (grow >= 0.0f ? BAR_HALF : BAR_HALF - w), 4);

    /* The bar's colour follows the SPLIT, not the lap, because that is what its
     * position means. Taking it from the panel state would draw a gaining bar
     * in losing red on any lap already thrown away, saying two opposite things
     * at once. A red panel with a green bar is the honest reading: this lap is
     * gone, but right now you are up. */
    uint32_t c = pegged ? T.warn : (s_split_delta < 0.0f ? T.good : T.bad);
    if ((int32_t)c != s_painted_bar_col) {
        lv_obj_set_style_bg_color(bar_fill, C(c), 0);
        s_painted_bar_col = (int32_t)c;
    }
}

static void refresh_clock_box(void)
{
    if (!clock_box) return;
    lv_obj_set_style_bg_color(clock_box, C(T.track), 0);
    lv_obj_set_style_border_color(clock_box, C(T.rule), 0);
    lv_obj_set_style_text_color(lbl_clock, C(T.fg), 0);
}

static void refresh_rail(void)
{
    if (!chip[0]) return;

    for (int i = 0; i < DASH2_SECTOR_COUNT; i++) {
        dash2_sector_state_t st = s_sectors[i].state;
        uint32_t col;
        switch (st) {
            case DASH2_SECTOR_ACTIVE: col = T.accent; break;
            case DASH2_SECTOR_FASTER: col = T.good;   break;
            case DASH2_SECTOR_SLOWER: col = T.bad;    break;
            /* Neutral, not green or red: there is no comparison behind it. */
            case DASH2_SECTOR_TIMED:  col = T.fg2;    break;
            default:                  col = T.muted;  break;
        }
        bool closed = (st == DASH2_SECTOR_FASTER || st == DASH2_SECTOR_SLOWER ||
                       st == DASH2_SECTOR_TIMED);

        lv_obj_set_style_bg_color(chip[i], C(T.track), 0);
        lv_obj_set_style_border_color(chip[i],
            C(st == DASH2_SECTOR_PENDING ? T.rule : col), 0);
        lv_obj_set_style_border_opa(chip[i], closed ? LV_OPA_40 : LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(chip_tag[i], C(col), 0);
        lv_obj_set_style_text_color(chip_val[i], C(col), 0);

        /* Decided once: the font is a LAYOUT property, so setting it dirties
         * the cell and its parent's layout. */
        lv_obj_set_style_text_font(chip_val[i],
            st == DASH2_SECTOR_PENDING ? &lv_font_montserrat_16 : DASH2_FONT_PILL, 0);

        /* Closed shows its delta, driven shows its running split, not yet
         * reached shows a small quiet dash — in the big face "--" would pull
         * more attention than the real numbers beside it. */
        if (st == DASH2_SECTOR_FASTER || st == DASH2_SECTOR_SLOWER) {
            float d = s_sectors[i].delta;
            /* One decimal: "±N.NN" in tabular digits is ~100 px against an 86 px
             * cell, one decimal fits at 77 px — and with hAcc around a metre the
             * second decimal was a column of noise. Past ten seconds the
             * fraction goes too. */
            const char *sign = d < 0 ? "-" : "+";
            float a = fabsf(d);
            if (a >= 99.0f)      lv_label_set_text_fmt(chip_val[i], "%s99", sign);
            else if (a >= 10.0f) lv_label_set_text_fmt(chip_val[i], "%s%.0f", sign, a);
            else                 lv_label_set_text_fmt(chip_val[i], "%s%.1f", sign, a);
        } else if (st == DASH2_SECTOR_TIMED) {
            /* A split, not a delta: unsigned, so the two cannot be confused, and
             * zero-padded to the same width as the running split it replaces, so
             * the number does not jump format at the moment the sector closes. */
            float t = s_sectors[i].delta;
            if (t > 99.9f) t = 99.9f;
            lv_label_set_text_fmt(chip_val[i], "%04.1f", t);
        } else if (st == DASH2_SECTOR_ACTIVE) {
            lv_label_set_text(chip_val[i], s_running);
        } else {
            lv_label_set_text(chip_val[i], "--");
        }
    }
}

static void apply_theme(void)
{
    lv_obj_set_style_bg_color(scr, C(T.bg), 0);
    lv_obj_set_style_text_color(lbl_speed_cap,  C(T.muted), 0);
    lv_obj_set_style_text_color(lbl_speed_unit, C(T.muted), 0);
    lv_obj_set_style_text_color(lbl_speed_v,    C(T.fg),    0);
    lv_obj_set_style_text_color(lbl_lap_n, C(T.muted), 0);
    lv_obj_set_style_text_color(lbl_lap_v, C(T.fg),    0);
    lv_obj_set_style_text_color(lbl_best,  C(T.muted), 0);
    lv_obj_set_style_text_color(lbl_noref, C(T.muted), 0);
    lv_obj_set_style_bg_color(bar_track, C(T.bg),   0);
    lv_obj_set_style_bg_color(bar_mid,   C(T.rule), 0);
    s_painted_state   = -1;
    s_painted_bar_col = -1;
    refresh_delta();
    refresh_bar();
    refresh_clock_box();
    refresh_rail();
}

/* ============================================================================
 * Public API
 * ============================================================================ */
void ui_dash2_set_speed(int kmh)
{
    if (!lbl_speed_v) return;
    if (kmh < 0) kmh = 0;
    /* No realign: the label has a fixed width, so KM/H does not move. */
    lv_label_set_text_fmt(lbl_speed_v, "%d", kmh);
}

void ui_dash2_set_lap(int lap_num, const char *lap_time, const char *best_time)
{
    if (!lbl_lap_v) return;
    lv_label_set_text_fmt(lbl_lap_n, "LAP %d", lap_num);
    if (lap_time)  lv_label_set_text(lbl_lap_v, lap_time);
    if (best_time) lv_label_set_text_fmt(lbl_best, "BEST %s", best_time);
    lv_obj_align(box_lap, LV_ALIGN_TOP_RIGHT, -PAD_X, 40);
    lv_obj_align_to(lbl_best, box_lap, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 3);
}

void ui_dash2_set_delta(float seconds, dash2_delta_state_t state, bool has_value)
{
    s_lap_delta   = seconds;
    s_delta_state = state;
    s_delta_has   = has_value;
    refresh_delta();
}

void ui_dash2_set_lap_clock(const char *running)
{
    if (!lbl_clock || !running) return;
    lv_label_set_text(lbl_clock, running);
}

/* Bar only: one resize and one colour, no panel repaint, because this moves
 * far more often than the number above it. */
void ui_dash2_set_delta_bar(float split_seconds, bool valid)
{
    s_split_delta = split_seconds;
    s_split_has   = valid;
    refresh_bar();
}

void ui_dash2_set_predicted(const char *str)
{
    if (!lbl_pred || !str) return;
    lv_label_set_text(lbl_pred, str);
    lv_obj_align_to(lbl_pred_cap, lbl_pred, LV_ALIGN_OUT_TOP_RIGHT, 0, -2);
}

void ui_dash2_set_running_split(const char *split)
{
    if (!split) return;
    lv_strlcpy(s_running, split, sizeof(s_running));
    if (s_current >= 0 && s_current < DASH2_SECTOR_COUNT && chip_val[s_current])
        lv_label_set_text(chip_val[s_current], s_running);
}

void ui_dash2_reset_sectors(void)
{
    for (int i = 0; i < DASH2_SECTOR_COUNT; i++) {
        s_sectors[i].state = DASH2_SECTOR_PENDING;
        s_sectors[i].delta = 0.0f;
    }
    s_current = -1;
    refresh_rail();
}

void ui_dash2_set_sector(dash2_sector_t s, dash2_sector_state_t st, float delta_seconds)
{
    if (s < 0 || s >= DASH2_SECTOR_COUNT) return;
    s_sectors[s].state = st;
    s_sectors[s].delta = delta_seconds;
    if (st == DASH2_SECTOR_ACTIVE)      s_current = (int)s;
    else if (s_current == (int)s)       s_current = -1;
    refresh_rail();
}

void ui_dash2_set_mode(dash2_mode_t mode)
{
    s_mode = mode;
    T = (mode == DASH2_MODE_DAY) ? TOK_DAY : TOK_NIGHT;
    apply_theme();
}

dash2_mode_t ui_dash2_get_mode(void) { return s_mode; }

/* LOCAL: see the header. */
lv_obj_t *ui_dash2_get_speed_obj(void) { return lbl_speed_v; }
lv_obj_t *ui_dash2_get_clock_obj(void) { return clock_box; }
