/* ============================================================================
 * ui_dash2.h — Dash v2 (lap-delta hero + sector band), LVGL 9
 *
 * Adopted from the Claude Design project "kart-data-logger"
 * (lvgl-port/ui_dash2.{h,c}, port of "Dash Final v2.html"). Deviations are
 * marked LOCAL: so a future design sync can be diffed against upstream:
 *
 *   LOCAL: the status bar is NOT built here — the firmware keeps a persistent
 *          one on lv_layer_top(). Same geometry (26 px at y=0), so everything
 *          below lands where the mock says.
 *   LOCAL: hero fonts point at the generated Barlow Condensed faces.
 *   LOCAL: the layout below the header is re-cut for racing use — a delta
 *          panel with a centre-zero bar in place of the vs-best hero, and a
 *          lap clock plus three full-height sector cells in place of the
 *          12 px chips and the LAST readout. The header is untouched.
 *   LOCAL: the delta is live rather than one-shot at the line, so
 *          ui_dash2_set_delta() takes a colour state and a has-value flag
 *          instead of a bare signed float, and the TOK_* palettes gain
 *          best/best_deep for the session-best purple.
 * ========================================================================= */
#ifndef UI_DASH2_H
#define UI_DASH2_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if LVGL_VERSION_MAJOR < 9
#error "ui_dash2 targets LVGL 9.x"
#endif

/* LOCAL: real faces rather than the montserrat fallbacks. Digits, ':', '.',
 * '+' and '-' only — see lib/DashFonts/DashFonts.h. Never put letters in a
 * label using these. */
#include "DashFonts.h"
#define DASH2_FONT_DELTA  &font_delta_96
#define DASH2_FONT_SPEED  &font_speed_72
#define DASH2_FONT_LAP    &font_lap_60
#define DASH2_FONT_PILL   &font_pill_44

/* Exported so a host harness renders at the size the firmware draws at. */
#define DASH2_SCR_W 480
#define DASH2_SCR_H 320

typedef enum {
    DASH2_MODE_NIGHT = 0,
    DASH2_MODE_DAY   = 1,
} dash2_mode_t;

typedef enum {
    DASH2_S1  = 0,
    DASH2_S2  = 1,
    DASH2_END = 2,
    DASH2_SECTOR_COUNT = 3,
} dash2_sector_t;

/* NONE is the reading before a reference lap exists. BEST is the motorsport
 * purple, fired at the line on the lap that became the session's fastest:
 * during a lap the panel stays green or red, because whether the lap will be a
 * best is not knowable until it ends. */
typedef enum {
    DASH2_DELTA_NONE = 0,
    DASH2_DELTA_FASTER,
    DASH2_DELTA_SLOWER,
    DASH2_DELTA_BEST,
} dash2_delta_state_t;

typedef enum {
    DASH2_SECTOR_PENDING = 0,   /* not reached this lap, or its gate was missed */
    DASH2_SECTOR_ACTIVE,        /* being driven — shows its running split       */
    DASH2_SECTOR_FASTER,        /* closed, up on its own best                   */
    DASH2_SECTOR_SLOWER,        /* closed, down on it                           */
    /* Closed with nothing to compare against — the whole of the first timed
     * lap. It still has a split TIME, so it shows that, unsigned and neutral;
     * without this state the cell blanks itself the instant it closes. */
    DASH2_SECTOR_TIMED,
} dash2_sector_state_t;

/* Build the dash on `parent` (NULL = active screen). Call once. */
void ui_dash2_init(lv_obj_t *parent);

void ui_dash2_set_speed(int kmh);
void ui_dash2_set_lap(int lap_num, const char *lap_time, const char *best_time);
/* `state` picks the colour pair, `has_value` the number. Repaints the panel
 * and its four labels, so call it only when what it draws has changed. */
void ui_dash2_set_delta(float seconds, dash2_delta_state_t state, bool has_value);

/* Full scale of the delta bar, seconds either side of centre. Restated from
 * LapManager::LAP_BAR_FULLSCALE_MS (this module is plain C and builds
 * standalone under tools/dashshot); keep the two in step. */
#define DASH2_BAR_FULLSCALE_S 1.0f

/* Split-relative, NOT the cumulative lap delta. Gaining grows right, losing
 * grows left, which is the opposite of the delta's sign. One resize and one
 * colour, because this moves far more often than the number above it. */
void ui_dash2_set_delta_bar(float split_seconds, bool valid);

/* Predicted lap: the reference lap plus the delta you are carrying. */
void ui_dash2_set_predicted(const char *str);

/* Lap clock, bottom-left. Touches one label; called every frame. */
void ui_dash2_set_lap_clock(const char *running);

/* Running split, shown in the sector cell currently being driven. */
void ui_dash2_set_running_split(const char *split);
void ui_dash2_reset_sectors(void);
void ui_dash2_set_sector(dash2_sector_t s, dash2_sector_state_t st, float delta_seconds);

void ui_dash2_set_mode(dash2_mode_t mode);
dash2_mode_t ui_dash2_get_mode(void);

/* LOCAL: the speed readout is the way into the setup menu. Upstream has no
 * navigation, so the firmware attaches its own handler here. */
lv_obj_t *ui_dash2_get_speed_obj(void);

/* LOCAL: the lap clock is a tap target too, for DEMO mode's skip-to-next-lap.
 * Same arrangement as the speed readout above. */
lv_obj_t *ui_dash2_get_clock_obj(void);

#ifdef __cplusplus
}
#endif
#endif /* UI_DASH2_H */
