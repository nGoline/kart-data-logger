#include "uiHelper.h"

/* Status bar uses an amber 'warn' tone in addition to good/bad. */
#define DASH_WARN_HEX 0xFFB020

static dash_theme_t T = THEME_NIGHT;
static UiHelper *s_instance = nullptr;

// Static member definitions
const char   *UiHelper::s_track_names[SETUP_MAX_TRACKS] = {};
int           UiHelper::s_track_count = 0;
int           UiHelper::s_track_idx   = 0;
bool          UiHelper::s_dirty       = false;
setup_coord_t UiHelper::s_line_l      = {};
setup_coord_t UiHelper::s_line_r      = {};

/* helper */
static inline lv_color_t C(uint32_t hex) { return lv_color_hex(hex); }

// C bridge called from lib/ui/ui_theme.cpp (0=dark, 1=light)
extern "C" void ui_helper_set_theme(int mode) {
    if (s_instance) s_instance->setTheme(mode == 0 ? DASH_MODE_NIGHT : DASH_MODE_DAY);
}

void UiHelper::init() {
    s_instance = this;
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = {
            .task_priority     = 4,
            .task_stack        = 16384,
            .task_affinity     = -1,
            .task_max_sleep_ms = 500,
            .timer_period_ms   = 5,
        },
        .rotate = LV_DISPLAY_ROTATION_90,
    };
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    bsp_display_lock(0);

    // Initialize the UI
    ui_init();

    //Reparent the status bar panel to the persistent top layer
    lv_obj_set_parent(ui_panelstatus, lv_layer_top());
    // Delete the now-empty Screen_TopBar to save RAM
    lv_obj_delete(ui_statusbarscreen);

    // Set the theme as saved
    setTheme(DASH_MODE_NIGHT);
    setSpeed(0);
    setGx(0);
    setGy(0);
    setDelta(0, true);
    setLap(0, "", "");

    // Track setup initialization
    s_track_count = 0;
    s_track_idx   = 0;
    s_dirty       = false;
    s_line_l      = { 0, 0, false };
    s_line_r      = { 0, 0, false };

    refresh_track_name();
    refresh_coord_row(SETUP_LINE_L);
    refresh_coord_row(SETUP_LINE_R);
    refresh_dirty();

    bsp_display_unlock();
}

/* ============================================================================
 * UPDATE
 * ============================================================================ */
void UiHelper::setSpeed(int kmh) {
    if (!ui_labelspeedvar) return;
    lv_label_set_text_fmt(ui_labelspeedvar, "%d", kmh);
}

void UiHelper::setGx(float gx) {
    if (!ui_bargx) return;
    int v = (int)(gx * 100.0f);
    if (v >  300) v =  300;
    if (v < -300) v = -300;
    lv_bar_set_value(ui_bargx, v, LV_ANIM_ON);
    lv_label_set_text_fmt(ui_labelgxvar, "G-X %.2f", gx);
}

void UiHelper::setGy(float gy) {
    if (!ui_bargy) return;
    /* invert sign so that acceleration (negative gy) fills above center,
     * matching the web prototype. */
    int v = (int)(-gy * 100.0f);
    if (v >  200) v =  200;
    if (v < -200) v = -200;
    lv_bar_set_value(ui_bargy, v, LV_ANIM_ON);
    lv_label_set_text_fmt(ui_labelgyvar, "%.2f G-Y", gy);
}

void UiHelper::setLap(uint8_t lap_num, const char *lap_str, const char *best_str) {
    if (ui_labellapnum) lv_label_set_text_fmt(ui_labellapnum, "LAP %d", lap_num);
    if (ui_labellapvar && lap_str)  lv_label_set_text(ui_labellapvar, lap_str);
    if (ui_labellapbest  && best_str) lv_label_set_text_fmt(ui_labellapbest, "BEST %s", best_str);
}

void UiHelper::setDelta(float seconds, bool faster) {
    if (!ui_paneldelta) return;
    uint32_t bg = faster ? T.good_deep : T.bad_deep;
    uint32_t fg = faster ? T.good      : T.bad;
    lv_obj_set_style_bg_color    (ui_paneldelta,       C(bg), 0);
    lv_obj_set_style_border_color(ui_paneldelta,       C(fg), 0);
    lv_obj_set_style_text_color  (ui_labelarrow, C(fg), 0);
    lv_obj_set_style_text_color  (ui_labeldeltavar,     C(fg), 0);
    lv_obj_set_style_text_color  (ui_labeldeltas,     C(fg), 0);
    lv_label_set_text(ui_labelarrow, faster ? LV_SYMBOL_UP : LV_SYMBOL_DOWN);
    lv_label_set_text_fmt(ui_labeldeltavar, "%.2f", fabsf(seconds));
}

void UiHelper::setHelmet(uint8_t pct) {
    if (!ui_labelvarhelmet) return;

    if (pct == 255)
        lv_label_set_text(ui_labelvarhelmet, "--");
    else {
        uint32_t hc = batt_color(pct);
        lv_label_set_text_fmt(ui_labelvarhelmet, "%d%%", pct);
        lv_obj_set_style_text_color(ui_labelvarhelmet, C(hc), 0);
    }
}

void UiHelper::hideHelmet() {
    lv_obj_add_flag(ui_panelhelmet, LV_OBJ_FLAG_HIDDEN);
}

void UiHelper::setDisplay(uint8_t pct) {
    if (!ui_labelvardisplay) return;

    if (pct == 255)
        lv_label_set_text(ui_labelvardisplay, "--");
    else {
        uint32_t hc = batt_color(pct);
        lv_label_set_text_fmt(ui_labelvardisplay, "%d%%", pct);
        lv_obj_set_style_text_color(ui_labelvardisplay, C(hc), 0);
    }
}

void UiHelper::setGps(uint8_t pct) {
    if (!ui_labelvargps) return;

    if (pct == 255)
        lv_label_set_text(ui_labelvargps, "--");
    else {
        uint32_t hc = gps_color(pct);
        lv_label_set_text_fmt(ui_labelvargps, "%d", pct);
        lv_obj_set_style_text_color(ui_labelvargps, C(hc), 0);
    }
}

void UiHelper::setPps(uint8_t expected_pps, uint8_t pps) {
    if (!ui_labelvaresp) return;

    uint32_t hc = esp_color(expected_pps, pps);
    lv_label_set_text_fmt(ui_labelvaresp, "%d", pps);
    lv_obj_set_style_text_color(ui_labelvaresp, C(hc), 0);
}

/* ============================================================================
 * TRACK SETUP
 * ============================================================================ */

void UiHelper::setTracks(const char *const *names, int count) {
    if (count < 0) count = 0;
    if (count > SETUP_MAX_TRACKS) count = SETUP_MAX_TRACKS;
    s_track_count = count;
    for (int i = 0; i < count; i++) s_track_names[i] = names[i];
    if (s_track_idx >= s_track_count) s_track_idx = 0;
    refresh_track_name();
}

void UiHelper::setTrackIdx(int idx) {
    if (s_track_count == 0) return;
    if (idx < 0) idx = 0;
    if (idx >= s_track_count) idx = s_track_count - 1;
    s_track_idx = idx;
    refresh_track_name();
}

void UiHelper::setStartL(double lat, double lon, bool valid) {
    s_line_l.lat = lat; s_line_l.lon = lon; s_line_l.valid = valid;
    refresh_coord_row(SETUP_LINE_L);
}

void UiHelper::setStartR(double lat, double lon, bool valid) {
    s_line_r.lat = lat; s_line_r.lon = lon; s_line_r.valid = valid;
    refresh_coord_row(SETUP_LINE_R);
}

void UiHelper::setDirty(bool dirty) {
    s_dirty = dirty;
    refresh_dirty();
}

/* ============================================================================
 * THEME
 * Status bar and dashboard are always dark
 * ============================================================================ */
void UiHelper::setTheme(dash_mode_t mode) {
    T = (mode == DASH_MODE_DAY) ? THEME_DAY : THEME_NIGHT;

    /* ----- Config ----- */
    lv_obj_set_style_bg_color(ui_configscreen,      C(T.bg), LV_PART_MAIN);

    // Header
    lv_obj_set_style_bg_color    (ui_panelsetup,        C(T.bg),        LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_panelsetup,        C(T.rule),      0);
    lv_obj_set_style_bg_color    (ui_panelsetupback,    C(T.bg),        LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color    (ui_panelsetupback,    C(T.surface2),  LV_PART_MAIN| LV_STATE_PRESSED);
    lv_obj_set_style_text_color  (ui_labelback,         C(T.fg2),       LV_PART_MAIN);
    lv_obj_set_style_text_color  (ui_labelbacktext,     C(T.fg2),       LV_PART_MAIN);
    lv_obj_set_style_text_color  (ui_labelsetup,        C(T.fg),        LV_PART_MAIN);
    // Buttons
    lv_obj_set_style_bg_color  (ui_panelsetupbuttons,   C(T.bg),        LV_PART_MAIN);
    lv_obj_set_style_bg_color  (ui_buttontracksetup,    C(T.surface2),  LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_labeltracksetuptext, C(T.fg),        LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_labeltracksetup,     C(T.fg),        LV_PART_MAIN);
    lv_obj_set_style_bg_color  (ui_buttonstartsession,  C(T.surface2),  LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_labelstartsession,   C(T.fg),        LV_PART_MAIN);
    lv_obj_set_style_bg_color  (ui_paneldarklight,      C(T.bg),        LV_PART_MAIN);
    // Theme Selector
    if (ui_buttondark) {
        bool night = (mode == DASH_MODE_NIGHT);
        lv_obj_t *active_btn   = night ? ui_buttondark            : ui_buttondark1;
        lv_obj_t *active_lbl   = night ? ui_labeltracksetuptext2  : ui_labeltracksetuptext1;
        lv_obj_t *inactive_btn = night ? ui_buttondark1           : ui_buttondark;
        lv_obj_t *inactive_lbl = night ? ui_labeltracksetuptext1  : ui_labeltracksetuptext2;
        lv_obj_set_style_bg_color  (active_btn,   C(T.accent),      LV_PART_MAIN);
        lv_obj_set_style_text_color(active_lbl,   C(T.fg),          LV_PART_MAIN);
        lv_obj_set_style_bg_color  (inactive_btn, C(T.surface2),    LV_PART_MAIN);
        lv_obj_set_style_text_color(inactive_lbl, C(T.fg),          LV_PART_MAIN);
    }

    /* ----- Track setup ----- */
    lv_obj_set_style_bg_color(ui_trackscreen, C(T.bg), LV_PART_MAIN);
    // Header
    lv_obj_set_style_bg_color    (ui_paneltracksetup,       C(T.bg),        LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_paneltracksetup,       C(T.rule),      0);
    lv_obj_set_style_bg_color    (ui_paneltracksetupback,   C(T.bg),        LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color    (ui_paneltracksetupback,   C(T.surface2),  LV_PART_MAIN| LV_STATE_PRESSED);
    lv_obj_set_style_text_color  (ui_labeltrackback,        C(T.fg2),       LV_PART_MAIN);
    lv_obj_set_style_text_color  (ui_labeltrackbacktext,    C(T.fg2),       LV_PART_MAIN);
    lv_obj_set_style_text_color  (ui_labeltracksetup1,      C(T.fg),        LV_PART_MAIN);
    lv_obj_set_style_bg_color    (ui_paneldirty,            C(T.bg),        LV_PART_MAIN);
    lv_obj_set_style_bg_color    (ui_dirtydot,              C(T.accent),    LV_PART_MAIN);
    lv_obj_set_style_shadow_color(ui_dirtydot,              C(T.accent),    LV_PART_MAIN );
    lv_obj_set_style_text_color  (ui_labeldirtytext,        C(T.muted),     LV_PART_MAIN);
    // Body
    lv_obj_set_style_bg_color(ui_panelbody, C(T.bg), LV_PART_MAIN);
    // Track Selector
    lv_obj_set_style_bg_color    (ui_paneltrack,            C(T.bg),        LV_PART_MAIN);
    lv_obj_set_style_bg_color    (ui_paneltrackheader,      C(T.bg),        LV_PART_MAIN);
    lv_obj_set_style_text_color  (ui_labeltrackheader,      C(T.muted),     LV_PART_MAIN);
    lv_obj_set_style_text_color  (ui_labeltrackpos,         C(T.muted),     LV_PART_MAIN);
    lv_obj_set_style_bg_color    (ui_paneltrackstepper,     C(T.bg),        LV_PART_MAIN);
    lv_obj_set_style_bg_color    (ui_panelstepper,          C(T.surface),   LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_panelstepper,          C(T.rule),      0);
    lv_obj_set_flex_grow         (ui_panelstepper,          1);
    lv_obj_set_style_bg_color    (ui_buttonstepperl,        C(T.surface2),  LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color    (ui_buttonstepperl,        C(T.muted),     LV_PART_MAIN| LV_STATE_PRESSED);
    lv_obj_set_style_border_color(ui_buttonstepperl,        C(T.rule),      0);
    lv_obj_set_style_text_color  (ui_labelstepperleft,      C(T.fg),        LV_PART_MAIN);
    lv_obj_set_style_text_color  (ui_labelsteppertrackname, C(T.fg),        LV_PART_MAIN);
    lv_obj_set_flex_grow         (ui_labelsteppertrackname, 1);
    lv_obj_set_style_bg_color    (ui_buttonstepperr,        C(T.surface2),  LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color    (ui_buttonstepperr,        C(T.muted),     LV_PART_MAIN| LV_STATE_PRESSED);
    lv_obj_set_style_border_color(ui_buttonstepperr,        C(T.rule),      0);
    lv_obj_set_style_text_color  (ui_labelstepperright,     C(T.fg),        LV_PART_MAIN);
    lv_obj_set_style_bg_color    (ui_buttonaddtrack,        C(T.surface2),  LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color    (ui_buttonaddtrack,        C(T.muted),     LV_PART_MAIN| LV_STATE_PRESSED);
    lv_obj_set_style_border_color(ui_buttonaddtrack,        C(T.rule),      0);
    lv_obj_set_style_text_color  (ui_labeladdtrack,         C(T.fg),        LV_PART_MAIN);

    // Coordinates
    lv_obj_set_style_bg_color  (ui_panelcoord,          C(T.bg),    LV_PART_MAIN);
    lv_obj_set_style_bg_color  (ui_panelstartline,      C(T.bg),    LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_labelstartline,      C(T.muted), LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_labelstartlinehint,  C(T.muted), LV_PART_MAIN);
    // Left Point
    lv_obj_set_style_bg_color    (ui_panellinel,    C(T.surface),   LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_panellinel,    C(T.rule),      0);
    lv_obj_set_style_text_color  (ui_labeltagl,     C(T.accent),    LV_PART_MAIN);
    lv_obj_set_style_text_color  (ui_labellatl,     C(T.fg),        LV_PART_MAIN);
    lv_obj_set_flex_grow         (ui_labellatl,     1);
    lv_obj_set_style_text_color  (ui_labellonl,     C(T.fg),        LV_PART_MAIN);
    lv_obj_set_flex_grow         (ui_labellonl,     1);
    lv_obj_set_style_text_color  (ui_labelemptyl,   C(T.muted),     LV_PART_MAIN);
    lv_obj_set_flex_grow         (ui_labelemptyl,   2);
    lv_obj_set_style_bg_color    (ui_buttonpinl,    C(T.surface2),  LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color    (ui_buttonpinl,    C(T.accent),    LV_PART_MAIN| LV_STATE_PRESSED);
    lv_obj_set_style_border_color(ui_buttonpinl,    C(T.rule),      0);
    lv_obj_set_style_text_color  (ui_labelpinl,     C(T.fg),        LV_PART_MAIN);
    // Right Point
    lv_obj_set_style_bg_color    (ui_panelliner,    C(T.surface),   LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_panelliner,    C(T.rule),      0);
    lv_obj_set_style_text_color  (ui_labeltagr,     C(T.accent),    LV_PART_MAIN);
    lv_obj_set_style_text_color  (ui_labellatr,     C(T.fg),        LV_PART_MAIN);
    lv_obj_set_flex_grow         (ui_labellatr,     1);
    lv_obj_set_style_text_color  (ui_labellonr,     C(T.fg),        LV_PART_MAIN);
    lv_obj_set_flex_grow         (ui_labellonr,     1);
    lv_obj_set_style_text_color  (ui_labelemptyr,   C(T.muted),     LV_PART_MAIN);
    lv_obj_set_flex_grow         (ui_labelemptyr,   2);
    lv_obj_set_style_bg_color    (ui_buttonpinr,    C(T.surface2),  LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color    (ui_buttonpinr,    C(T.accent),    LV_PART_MAIN| LV_STATE_PRESSED);
    lv_obj_set_style_border_color(ui_buttonpinr,    C(T.rule),      0);
    lv_obj_set_style_text_color  (ui_labelpinr,     C(T.fg),        LV_PART_MAIN);
    // Action Bar
    lv_obj_set_style_bg_color    (ui_panelactionbar,    C(T.bg),    LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_panelactionbar,    C(T.rule),  0);
    // Cancel
    lv_obj_set_style_bg_color  (ui_buttoncancel,    C(T.surface),   LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color  (ui_buttoncancel,    C(T.surface2),  LV_PART_MAIN| LV_STATE_PRESSED);
    lv_obj_set_flex_grow       (ui_buttoncancel,    1);
    lv_obj_set_style_text_color(ui_labelcancel,     C(T.fg2),        LV_PART_MAIN);
    // Save
    lv_obj_set_style_bg_color  (ui_buttonsave,      C(T.accent),    LV_PART_MAIN| LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color  (ui_buttonsave,      C(T.surface2),  LV_PART_MAIN| LV_STATE_PRESSED);
    lv_obj_set_style_text_color(ui_buttonsave,      C(T.accent_fg), LV_PART_MAIN);
    lv_obj_set_flex_grow       (ui_buttonsave,      14);
    lv_obj_set_flex_grow       (ui_buttoncancel,    10);
}

/* ============================================================================
 * SESSION STATE
 * ============================================================================ */
void UiHelper::setSessionState(bool active) {
    if (ui_labelstartsession)
        lv_label_set_text(ui_labelstartsession, active ? "STOP SESSION" : "START SESSION");

    if (ui_panelrecording) {
        if (active)
            lv_obj_remove_flag(ui_panelrecording, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(ui_panelrecording, LV_OBJ_FLAG_HIDDEN);
    }
}

void UiHelper::tickRecordingPanel() {
    if (!ui_panelrecording || lv_obj_has_flag(ui_panelrecording, LV_OBJ_FLAG_HIDDEN)) return;
    uint32_t c = (millis() / 500) % 2 ? T.bad : T.bad_deep;
    lv_obj_set_style_bg_color(ui_panelrecording, C(c), LV_PART_MAIN);
}

/* ============================================================================
 * STATUS BAR SETTERS
 * ============================================================================ */
uint32_t UiHelper::batt_color(uint8_t pct) {
    if (pct >= 50) return T.good;
    if (pct >= 20 || (millis() / 500) % 2) return DASH_WARN_HEX;
    return T.bad;
}
uint32_t UiHelper::gps_color(uint8_t n) {
    if (n >= 8) return T.good;
    if (n >= 4 || (millis() / 500) % 2) return DASH_WARN_HEX;
    return T.bad;
}
uint32_t UiHelper::esp_color(uint8_t expected_pps, uint8_t pps) {
    if (pps >= expected_pps) return T.good;
    if (pps >= expected_pps / 2  || (millis() / 500) % 2) return DASH_WARN_HEX;
    return T.bad;
}

/* ============================================================================
 * TRACK SETUP - REFRESHERS
 * ============================================================================ */
void UiHelper::refresh_track_name(void) {
    if (!ui_labelsteppertrackname) return;
    if (s_track_count == 0) {
        lv_label_set_text(ui_labelsteppertrackname, "NO TRACKS");
        lv_label_set_text(ui_labeltrackpos, "0 / 0");
        return;
    }
    lv_label_set_text(ui_labelsteppertrackname, s_track_names[s_track_idx]);
    lv_label_set_text_fmt(ui_labeltrackpos, "%d / %d", s_track_idx + 1, s_track_count);
}

void UiHelper::refresh_coord_row(setup_line_side_t side) {
    setup_coord_t *c    = (side == SETUP_LINE_L) ? &s_line_l : &s_line_r;
    lv_obj_t *lat       = (side == SETUP_LINE_L) ? ui_labellatl   : ui_labellatr;
    lv_obj_t *lon       = (side == SETUP_LINE_L) ? ui_labellonl   : ui_labellonr;
    lv_obj_t *empty     = (side == SETUP_LINE_L) ? ui_labelemptyl : ui_labelemptyr;
    lv_obj_t *pin_lbl   = (side == SETUP_LINE_L) ? ui_labelpinl   : ui_labelpinr;
    if (!lat) return;

    if (c->valid) {
        char buf[24];
        snprintf(buf, sizeof(buf), "LAT %.4f%c",
                 fabs(c->lat), c->lat >= 0 ? 'N' : 'S');
        lv_label_set_text(lat, buf);
        snprintf(buf, sizeof(buf), "LON %.4f%c",
                 fabs(c->lon), c->lon >= 0 ? 'E' : 'W');
        lv_label_set_text(lon, buf);
        lv_obj_clear_flag(lat,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lon,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag  (empty, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(pin_lbl, "RESET");
    } else {
        lv_obj_add_flag  (lat,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag  (lon,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(empty, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(pin_lbl, "PIN");
    }
}

void UiHelper::refresh_dirty(void) {
    if (!ui_dirtydot) return;
    if (s_dirty) {
        lv_obj_clear_flag(ui_dirtydot, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui_labeldirtytext, "UNSAVED");
        lv_obj_set_style_text_color(ui_labeldirtytext, C(T.accent), 0);
    } else {
        lv_obj_add_flag(ui_dirtydot, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ui_labeldirtytext, "SAVED");
        lv_obj_set_style_text_color(ui_labeldirtytext, C(T.muted), 0);
    }
}

/* ============================================================================
 * TRACK SETUP — C BRIDGES (called from ui_theme.cpp)
 * ============================================================================ */
extern "C" int  ui_helper_get_track_idx()      { return s_instance ? s_instance->getTrackIdx() : 0; }
extern "C" void ui_helper_set_track_idx(int i) { if (s_instance) s_instance->setTrackIdx(i); }
extern "C" void ui_helper_set_dirty(bool d)    { if (s_instance) s_instance->setDirty(d); }

extern "C" void ui_helper_set_start_l(double lat, double lon, bool valid) {
    if (s_instance) s_instance->setStartL(lat, lon, valid);
}
extern "C" void ui_helper_set_start_r(double lat, double lon, bool valid) {
    if (s_instance) s_instance->setStartR(lat, lon, valid);
}
extern "C" void ui_helper_set_track_names(const char *const *names, int n) {
    if (s_instance) s_instance->setTracks(names, n);
}