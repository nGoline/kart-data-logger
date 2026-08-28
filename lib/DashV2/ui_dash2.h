/* ============================================================================
 * ui_dash2.h — Dash v2 (lap-delta hero + sector band), LVGL 9
 *
 * Adopted from the Claude Design project "kart-data-logger"
 * (lvgl-port/ui_dash2.{h,c}, port of "Dash Final v2.html"). Local changes are
 * marked LOCAL: so a future design sync can be diffed against upstream:
 *
 *   LOCAL: the status bar is NOT built here. This firmware keeps a persistent
 *          bar on lv_layer_top() so it survives across the config, track and
 *          sessions screens, and it carries CAM / WIFI / REC cells upstream
 *          knows nothing about. Geometry is unchanged — upstream's bar is also
 *          26 px at y=0, so everything below still lands where the mock says.
 *   LOCAL: hero fonts point at the generated Barlow Condensed faces.
 *   LOCAL: the layout below the header is substantially re-cut for racing use,
 *          driven by the fact that nothing numeric can be read mid-corner:
 *            - upstream's vs-best hero becomes a delta PANEL carrying a
 *              centre-zero bar (peripheral, split-relative) alongside the
 *              cumulative number and a predicted lap time.
 *            - the bottom band is the lap clock plus three full-height sector
 *              cells, replacing the 12 px chips and the LAST readout, which
 *              duplicated the header's lap time.
 *            - the blinking in-sector dot is gone; the running split now shows
 *              in whichever sector cell is active.
 *          The header (SPEED, LAP n + lap time, BEST) is untouched.
 *   LOCAL: the delta is live rather than one-shot at the line, so
 *          ui_dash2_set_delta() takes a colour state and a has-value flag
 *          instead of a bare signed float, and the TOK_* palettes gain
 *          best/best_deep for the session-best purple plus peg for a bar that
 *          has run off its scale. The numbers come from LapManager's
 *          reference trace.
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

/* Colour states of the lap-delta hero panel. NONE is the honest reading before
 * a reference lap exists: there is nothing to be up or down on yet. BEST is
 * the motorsport purple, fired at the line on the lap that became the session's
 * fastest; during a lap the panel stays green or red, because whether the lap
 * will be a best is not knowable until it ends. */
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
    /* Closed, but with nothing to compare against — the whole of the first
     * timed lap, and any sector whose best has not been set yet. It still has a
     * split TIME, so it shows that, unsigned and in a neutral colour. Without
     * this state such a sector fell through to PENDING and blanked itself the
     * instant it closed, which made the rail useless for a driver's first lap
     * out: each cell counted up, then went back to "--". */
    DASH2_SECTOR_TIMED,
} dash2_sector_state_t;

/* Build the dash on `parent` (NULL = active screen). Call once. */
void ui_dash2_init(lv_obj_t *parent);

void ui_dash2_set_speed(int kmh);
void ui_dash2_set_lap(int lap_num, const char *lap_time, const char *best_time);
/* The hero panel. `state` picks the colour pair and `has_value` the number:
 * false blanks it to "--.--", for NONE and for a first session best that has
 * nothing to be compared against. Repaints the panel and its four labels on
 * every call, so call it only when what it draws has actually changed. */
void ui_dash2_set_delta(float seconds, dash2_delta_state_t state, bool has_value);

/* Full-scale of the delta bar, in seconds either side of centre. Measured on
 * the 12-lap fixture: the split delta stays inside 0.73 s on a normal lap and
 * only 4.4% of all samples run past 1.00 s, so this leaves the bar expressive
 * without pegging it in ordinary driving. */
#define DASH2_BAR_FULLSCALE_S 1.0f

/* The delta bar. Split-relative, NOT the cumulative lap delta: a cumulative bar
 * pegs the instant you have a real off and then sits at the end of its scale
 * for the rest of the lap. Gaining grows right, losing grows left, which is the
 * opposite of the delta's sign. Cheap — one resize and one colour, no panel
 * repaint — because this moves far more often than the number above it. */
void ui_dash2_set_delta_bar(float split_seconds, bool valid);

/* Predicted lap: the reference lap plus the delta you are carrying. */
void ui_dash2_set_predicted(const char *str);

/* Lap clock, bottom-left. Updated every frame, so it touches one label. */
void ui_dash2_set_lap_clock(const char *running);

void ui_dash2_enter_sector(dash2_sector_t s);
void ui_dash2_close_sector(dash2_sector_t s, float delta_seconds);

/* Running split, shown in the sector cell currently being driven. */
void ui_dash2_set_running_split(const char *split);
void ui_dash2_reset_sectors(void);
void ui_dash2_set_sector(dash2_sector_t s, dash2_sector_state_t st, float delta_seconds);

void ui_dash2_set_mode(dash2_mode_t mode);
dash2_mode_t ui_dash2_get_mode(void);

/* LOCAL: the speed readout doubles as the way into the setup menu, as it did
 * on the screen this replaces. Upstream has no navigation of its own, so the
 * firmware attaches its own handler here rather than reaching into statics. */
lv_obj_t *ui_dash2_get_speed_obj(void);

#ifdef __cplusplus
}
#endif
#endif /* UI_DASH2_H */
