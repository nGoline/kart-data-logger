#include "uiHelper.h"

/* Status bar uses an amber 'warn' tone in addition to good/bad. */
#define DASH_WARN_HEX 0xFFB020

static dash_theme_t T = THEME_NIGHT;

/* helper */
static inline lv_color_t C(uint32_t hex) { return lv_color_hex(hex); }

void UiHelper::init() {
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = {
            .task_priority     = 4,
            .task_stack        = 16384,
            .task_affinity     = -1,
            .task_max_sleep_ms = 500,
            .timer_period_ms   = 5,
        },
        .rotate = LV_DISPLAY_ROTATION_270,
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
 * THEME
 * ============================================================================ */
void UiHelper::setTheme(dash_mode_t mode) {
    T = (mode == DASH_MODE_DAY) ? THEME_DAY : THEME_NIGHT;

    /* re-apply theme to all colored parts */
    lv_obj_set_style_bg_color(ui_dashboardscreen, C(T.bg), 0);

    lv_obj_set_style_text_color(ui_labelspeedlabel,  C(T.muted), 0);
    lv_obj_set_style_text_color(ui_labelspeedvar,    C(T.fg),    0);
    lv_obj_set_style_text_color(ui_labelspeedunit, C(T.muted), 0);

    lv_obj_set_style_text_color(ui_labellapnum, C(T.muted), 0);
    lv_obj_set_style_text_color(ui_labellapvar, C(T.fg),    0);
    lv_obj_set_style_text_color(ui_labellapbest,  C(T.muted), 0);

    lv_obj_set_style_bg_color(ui_bargy, C(T.track),  LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_bargy, C(T.accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(ui_bargx, C(T.track),  LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_bargx, C(T.accent), LV_PART_INDICATOR);

    lv_obj_set_style_text_color(ui_labelbargy,   C(T.muted), 0);
    lv_obj_set_style_text_color(ui_labelgxvar, C(T.fg),    0);
    lv_obj_set_style_text_color(ui_labelgyvar, C(T.fg),    0);

    lv_obj_set_style_bg_color(ui_panelstatus, C(T.bg), 0);
    lv_obj_set_style_border_color(ui_panelstatus, C(T.track), 0);
    lv_obj_set_style_text_color(ui_labeltaghelmet, C(T.muted), 0);
    lv_obj_set_style_text_color(ui_labeltagdisplay, C(T.muted), 0);
    lv_obj_set_style_text_color(ui_labeltaggps,  C(T.muted), 0);
    lv_obj_set_style_text_color(ui_labeltagesp,  C(T.muted), 0);
}

/* ----- status bar setters ----- */
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