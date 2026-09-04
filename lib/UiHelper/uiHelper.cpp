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
lv_obj_t     *UiHelper::s_cam_tag     = nullptr;
lv_obj_t     *UiHelper::s_cam_var     = nullptr;
bool          UiHelper::s_cam_linked  = false;
bool          UiHelper::s_cam_recording = false;
bool          UiHelper::s_cam_gpslock = false;
uint8_t       UiHelper::s_cam_batt    = 255;
lv_obj_t     *UiHelper::s_rec_panel    = nullptr;
lv_obj_t     *UiHelper::s_rec_dot      = nullptr;
lv_obj_t     *UiHelper::s_rec_lbl      = nullptr;
UiHelper::rec_cell_t UiHelper::s_rec_mode = UiHelper::REC_OFF;
lv_obj_t     *UiHelper::s_wifi_panel   = nullptr;
lv_obj_t     *UiHelper::s_wifi_var     = nullptr;
lv_obj_t     *UiHelper::s_wifi_btn_lbl = nullptr;
bool          UiHelper::s_wifi_running = false;
uint8_t       UiHelper::s_wifi_clients = 0;
char          UiHelper::s_wifi_ip[20]  = {0};

lv_obj_t *UiHelper::s_charge_screen = nullptr;
lv_obj_t *UiHelper::s_charge_pct    = nullptr;
lv_obj_t *UiHelper::s_charge_volts  = nullptr;

/* helper */
static inline lv_color_t C(uint32_t hex) { return lv_color_hex(hex); }

/* Implemented in src/main_display.cpp; up here because dash_speed_cb() calls
 * one of them. */
extern "C" void ui_helper_enter_demo_mode(void);
extern "C" void ui_helper_exit_demo_mode(void);
extern "C" void ui_helper_demo_skip_lap(void);

/* Tap the lap clock to skip the rest of the demo's lap. A no-op outside demo
 * mode, so the box is inert on a real session. */
static void dash_clock_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) ui_helper_demo_skip_lap();
}

/* Tap the speed readout to reach setup — the affordance the SquareLine
 * dashboard had, restored on the v2 screen. */
static void dash_speed_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    /* The way off the dashboard is also the way out of demo mode: synthetic
     * telemetry left running would put fake laps on the dash next time. */
    ui_helper_exit_demo_mode();
    _ui_screen_change(&ui_configscreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0,
                      &ui_configscreen_screen_init);
}

// C bridge called from lib/ui/ui_theme.cpp (0=dark, 1=light)
extern "C" void ui_helper_set_theme(int mode) {
    if (s_instance) s_instance->setTheme(mode == 0 ? DASH_MODE_NIGHT : DASH_MODE_DAY);
}

/* Implemented in src/main_display.cpp. These only ever set a flag — the real
 * work (GPS teardown, CPU rescaling) happens in loop(), never in LVGL context. */
extern "C" void ui_helper_enter_charge_mode(void);
extern "C" void ui_helper_exit_charge_mode(void);
extern "C" void ui_helper_charge_screen_touched(void);

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
        .rotate = LV_DISPLAY_ROTATION_270,
    };
    bsp_display_start_with_config(&cfg);
    bsp_display_backlight_on();

    bsp_display_lock(0);

    // Initialize the UI
    ui_init();

    // Swap SquareLine's dashboard for the v2 design (lib/DashV2). Done as a
    // swap rather than by editing the generated file so every navigation
    // reference to ui_dashboardscreen stays valid: build the replacement,
    // load it, then let SquareLine's own destructor delete the old screen and
    // NULL all of its widget globals — which is what stops the setters below
    // from touching freed objects.
    {
        lv_obj_t *fresh = lv_obj_create(NULL);
        ui_dash2_init(fresh);
        lv_screen_load(fresh);
        ui_dashboardscreen_screen_destroy();
        ui_dashboardscreen = fresh;

        // Tap the speed to reach setup, as the SquareLine dashboard did. That
        // handler lived on ui_labelspeedvar and died with it; labels are not
        // clickable by default, hence the flag and the fat hit area.
        lv_obj_t *spd = ui_dash2_get_speed_obj();
        if (spd) {
            lv_obj_add_flag(spd, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_ext_click_area(spd, 24);
            lv_obj_add_event_cb(spd, dash_speed_cb, LV_EVENT_CLICKED, NULL);
        }

        // The lap clock skips the demo's lap. Already big enough to hit, so
        // no extra click area.
        lv_obj_t *clk = ui_dash2_get_clock_obj();
        if (clk) {
            lv_obj_add_flag(clk, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(clk, dash_clock_cb, LV_EVENT_CLICKED, NULL);
        }
    }

    //Reparent the status bar panel to the persistent top layer
    lv_obj_set_parent(ui_panelstatus, lv_layer_top());
    // Delete the now-empty Screen_TopBar to save RAM
    lv_obj_delete(ui_statusbarscreen);

    // GoPro cell — added here rather than in SquareLine so the generated UI
    // stays untouched. The status bar is a SPACE_BETWEEN flex row, so this
    // lands between the DISP and GPS cells on its own.
    build_rec_cell();
    build_camera_cell();
    build_wifi_cell();
    build_wifi_button();

    // Set the theme as saved
    setTheme(DASH_MODE_NIGHT);
    setSpeed(0);
    setGx(0);
    setGy(0);
    setLiveDelta(0, false);   /* no reference lap yet: blank, not "0.00" */
    setLap(0, "", "");

    // Track setup initialization
    s_track_count = 0;
    s_track_idx   = 0;
    s_dirty       = false;
    s_line_l      = { 0, 0, false };
    s_line_r      = { 0, 0, false };

    refresh_track_name();
    build_sector_rows();
    refresh_coord_row(SETUP_LINE_L);
    refresh_coord_row(SETUP_LINE_R);
    refresh_dirty();

    // Charge mode UI. Both are built by hand rather than exported from
    // SquareLine, so re-exporting lib/ui/ does not wipe them.
    build_charge_screen();
    build_charge_mode_button();
    build_demo_button();
    build_version_label();
    build_alert_banner();

    bsp_display_unlock();
}

/* Normally injected by platformio.ini from `git describe`. Guarded so the file still
 * compiles if something builds it without the flag. */
#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif

/* Firmware version, right-aligned in the setup screen's header bar.
 *
 * The header is the only free space on that screen: ui_panelsetupbuttons is y=66
 * h=254 on a 320px panel, so it reaches the bottom edge exactly and there is no
 * footer to put this in. The header is not a flex container and its two existing
 * children sit at LEFT_MID (back) and CENTER (title), leaving RIGHT_MID empty.
 *
 * Barlow22 rather than one of the larger faces: the 60/52/200 exports are
 * digits-only subsets (cmap 46..58), and a commit hash is mostly letters — every
 * one would render as a missing-glyph box. 22 and 32 carry full ASCII 32..126.
 *
 * Built here for the same reason as the charge-mode button: a re-export of lib/ui/
 * from SquareLine cannot wipe it. */
void UiHelper::build_version_label(void) {
    if (!ui_panelsetup) return;

    lv_obj_t *lbl = lv_label_create(ui_panelsetup);
    lv_obj_set_width(lbl, LV_SIZE_CONTENT);
    lv_obj_set_height(lbl, LV_SIZE_CONTENT);
    lv_obj_set_align(lbl, LV_ALIGN_RIGHT_MID);
    lv_label_set_text(lbl, FW_VERSION);
    // Muted grey: it should be readable when looked for and ignorable otherwise.
    lv_obj_set_style_text_color(lbl, C(0x6B7280), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &ui_font_BarlowCondensedBold22, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/* ============================================================================
 * CHARGE MODE
 *
 * Both the charge screen and the config-screen button are built here at runtime
 * rather than exported from SquareLine, so a re-export of lib/ui/ cannot wipe
 * them. That is a workaround — they belong in the SquareLine project.
 *
 * When migrating:
 *  - Widen Barlow60's character range in SquareLine to include '%' (0x25). The
 *    current export is digits-only (cmap 46..58), which is why the percentage
 *    label had to drop to Barlow48 to avoid a missing-glyph box.
 *  - Delete build_charge_screen() / build_charge_mode_button() and repoint
 *    show/hide/setChargeBattery at the generated ui_* globals.
 *  - Keep the generated button's callback trivial. It must only set a flag:
 *    entering/leaving charge mode blocks for seconds (gps.begin() probes two
 *    baud rates), which would wedge the LVGL task while it holds the display
 *    lock. See the bridges in src/main_display.cpp.
 * ============================================================================ */

/* The config screen's big injected buttons (CHARGE MODE, DEMO): a left-aligned
 * label in the Barlow face on the darker surface, rather than
 * make_setup_button()'s styling. Built here so the two stay a pair. */
static lv_obj_t *build_config_column_button(const char *text, lv_event_cb_t cb) {
    if (!ui_panelsetupbuttons) return NULL;

    lv_obj_t *btn = lv_button_create(ui_panelsetupbuttons);
    lv_obj_set_width(btn, 250);
    lv_obj_set_height(btn, 50);
    lv_obj_set_align(btn, LV_ALIGN_TOP_MID);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_ext_click_area(btn, 5);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(btn, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, C(0x1A1D23), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, C(0x6B7280), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_flex_grow(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_width(lbl, LV_SIZE_CONTENT);
    lv_obj_set_height(lbl, LV_SIZE_CONTENT);
    lv_obj_set_align(lbl, LV_ALIGN_LEFT_MID);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, C(0xF6F8FB), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(lbl, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &ui_font_BarlowCondensedBold32, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

/* CHARGE MODE button, appended to the config screen's button column. */
void UiHelper::build_charge_mode_button(void) {
    build_config_column_button("CHARGE MODE", [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_CLICKED) ui_helper_enter_charge_mode();
    });
}

/* DEMO button, directly below CHARGE MODE. The callback only sets a flag:
 * entering demo mode repoints LapManager, which is not work for an LVGL
 * callback. */
void UiHelper::build_demo_button(void) {
    build_config_column_button("DEMO", [](lv_event_t *e) {
        if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
        ui_helper_enter_demo_mode();
        /* Straight to the dashboard — safe here because a button callback is
         * already in LVGL context. The mode itself comes up next loop(). */
        _ui_screen_change(&ui_dashboardscreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0,
                          &ui_dashboardscreen_screen_init);
    });
}

/* Charge screen: big battery readout plus an explicit RESUME button. A tap
 * anywhere else only wakes the backlight — it must not leave charge mode. */
void UiHelper::build_charge_screen(void) {
    s_charge_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_charge_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_charge_screen, C(0x050608), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_charge_screen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_charge_screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Any press on the screen background wakes the backlight.
    lv_obj_add_event_cb(s_charge_screen, [](lv_event_t *e) {
        if (lv_event_get_code(e) == LV_EVENT_PRESSED) ui_helper_charge_screen_touched();
    }, LV_EVENT_PRESSED, NULL);

    lv_obj_t *title = lv_label_create(s_charge_screen);
    lv_label_set_text(title, "CHARGING");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_text_color(title, C(0xFFD400), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title, &ui_font_BarlowCondensedBold32, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Barlow60/52/200 are digits-only subsets (cmap 46..58 / 48..57) — a '%'
    // renders as a missing-glyph box. Barlow48 carries full ASCII 32..126.
    s_charge_pct = lv_label_create(s_charge_screen);
    lv_label_set_text(s_charge_pct, "--%");
    lv_obj_align(s_charge_pct, LV_ALIGN_CENTER, 0, -22);
    lv_obj_set_style_text_color(s_charge_pct, C(0xF6F8FB), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_charge_pct, &ui_font_BarlowCondensedBold48, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Voltage is the number to trust here: the percentage is derived from the
    // charger rail while plugged in, so it reads high. See setChargeBattery().
    s_charge_volts = lv_label_create(s_charge_screen);
    lv_label_set_text(s_charge_volts, "--.-- V");
    lv_obj_align(s_charge_volts, LV_ALIGN_CENTER, 0, 24);
    lv_obj_set_style_text_color(s_charge_volts, C(0xCBD0D8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_charge_volts, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *hint = lv_label_create(s_charge_screen);
    lv_label_set_text(hint, "tap to wake the screen");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -76);
    lv_obj_set_style_text_color(hint, C(0x6B7280), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *resume = lv_button_create(s_charge_screen);
    lv_obj_set_width(resume, 200);
    lv_obj_set_height(resume, 50);
    lv_obj_align(resume, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_flex_flow(resume, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(resume, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_ext_click_area(resume, 8);
    lv_obj_remove_flag(resume, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(resume, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(resume, C(0x1A1D23), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(resume, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(resume, C(0xFFD400), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(resume, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(resume, 1, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *rlbl = lv_label_create(resume);
    lv_label_set_text(rlbl, "RESUME");
    lv_obj_set_style_text_color(rlbl, C(0xF6F8FB), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(rlbl, &ui_font_BarlowCondensedBold32, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Wake the backlight on press, leave charge mode on release. Without the
    // PRESSED half, tapping RESUME while the backlight is off would exit blind.
    lv_obj_add_event_cb(resume, [](lv_event_t *e) {
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED)      ui_helper_charge_screen_touched();
        else if (code == LV_EVENT_CLICKED) ui_helper_exit_charge_mode();
    }, LV_EVENT_ALL, NULL);
}

void UiHelper::showChargeScreen() {
    if (!s_charge_screen) return;
    // The status bar lives on the top layer, so it would float over the
    // charge screen unless it is explicitly hidden.
    if (ui_panelstatus) lv_obj_add_flag(ui_panelstatus, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(s_charge_screen);
}

void UiHelper::hideChargeScreen() {
    if (ui_panelstatus) lv_obj_remove_flag(ui_panelstatus, LV_OBJ_FLAG_HIDDEN);
    if (ui_dashboardscreen) lv_screen_load(ui_dashboardscreen);
}

void UiHelper::setChargeBattery(uint8_t pct, float volts) {
    if (!s_charge_pct) return;
    if (pct == 255) {
        lv_label_set_text(s_charge_pct, "--%");
    } else {
        // Leading '~': with no power-path charger the sense divider sits on the
        // charger rail, so this is the cell terminal voltage *under charge* and
        // reads well above its resting value. Marked approximate rather than
        // faked with an offset. ('~' is U+007E, inside Barlow48's ASCII range.)
        lv_label_set_text_fmt(s_charge_pct, "~%d%%", pct);
        lv_obj_set_style_text_color(s_charge_pct, C(batt_color(pct)), 0);
    }
    if (s_charge_volts) lv_label_set_text_fmt(s_charge_volts, "%.2f V", volts);
}

/* ============================================================================
 * UPDATE
 * ============================================================================ */
void UiHelper::setSpeed(int kmh) {
    ui_dash2_set_speed(kmh);
}

/* The v2 design drops the G-meter. Kept as no-ops so callers (and the IMU
 * build) need no #ifdefs — and nothing is lost today, since ENABLE_IMU is off
 * and these have been reporting a flat zero. */
void UiHelper::setGx(float gx) { (void)gx; }

void UiHelper::setGy(float gy) { (void)gy; }

void UiHelper::setLap(uint8_t lap_num, const char *lap_str, const char *best_str) {
    ui_dash2_set_lap((int)lap_num, lap_str, best_str);
}

/* ============================================================================
 * LAP DELTA HERO PANEL
 * The delta arrives per fix, so this gate keeps a 25 Hz number off a widget
 * that repaints itself and four labels whenever it is touched.
 * ============================================================================ */

/* ~5 Hz. Two decimals cannot be read faster than that, and a full-frame blit
 * per telemetry sample would cost the dashboard its frame rate. */
#define DELTA_MIN_REPAINT_MS 200

/* Colour hysteresis, in seconds. A live delta sits near zero for much of a lap
 * and a metre of hAcc is worth six hundredths at 60 km/h, so a bare `< 0` test
 * strobes green/red on noise. Only the colour is damped. */
#define DELTA_DEADBAND_S 0.05f

/* How long purple holds at the line before the new lap's live delta takes
 * over. Long enough to read coming past the pits, short enough that the first
 * corner is already being coached. */
#define DELTA_BEST_HOLD_MS 4000

static uint32_t s_delta_flash_until = 0;    /* 0 = not flashing */
static int32_t  s_delta_shown_cs    = INT32_MIN;
static int      s_delta_shown_state = -1;
static uint32_t s_delta_painted_ms  = 0;
static bool     s_delta_painted     = false;
static bool     s_delta_faster      = true; /* the deadband's remembered side */

void UiHelper::paint_delta(float seconds, bool valid, dash2_delta_state_t st,
                           uint32_t now, bool force) {
    /* Diff on what is actually drawn (the hundredth and the colour), not on
     * the float, which changes on every fix and would defeat the whole point. */
    int32_t cs = valid ? (int32_t)lroundf(seconds * 100.0f) : INT32_MIN;
    if (!force && s_delta_painted) {
        if (cs == s_delta_shown_cs && (int)st == s_delta_shown_state) return;
        if ((uint32_t)(now - s_delta_painted_ms) < DELTA_MIN_REPAINT_MS) return;
    }
    s_delta_painted     = true;
    s_delta_shown_cs    = cs;
    s_delta_shown_state = (int)st;
    s_delta_painted_ms  = now;
    ui_dash2_set_delta(valid ? seconds : 0.0f, st, valid);
}

void UiHelper::setLiveDelta(float seconds, bool valid) {
    uint32_t now = millis();

    /* Purple owns the panel until it expires. Signed compare so the hold ends
     * correctly across a millis() wrap. */
    if (s_delta_flash_until) {
        if ((int32_t)(now - s_delta_flash_until) < 0) return;
        s_delta_flash_until = 0;
    }

    dash2_delta_state_t st = DASH2_DELTA_NONE;
    if (valid) {
        if      (seconds >  DELTA_DEADBAND_S) s_delta_faster = false;
        else if (seconds < -DELTA_DEADBAND_S) s_delta_faster = true;
        /* Inside the band the last side stands, and it starts green, so an
         * exact zero reads as level rather than as behind. */
        st = s_delta_faster ? DASH2_DELTA_FASTER : DASH2_DELTA_SLOWER;
    }
    paint_delta(seconds, valid, st, now, false);
}

void UiHelper::flashBestLap(float seconds, bool valid) {
    uint32_t now = millis();
    s_delta_flash_until = now + DELTA_BEST_HOLD_MS;
    if (!s_delta_flash_until) s_delta_flash_until = 1;   /* 0 means "not flashing" */
    s_delta_faster = true;      /* the new lap starts level with the lap it chases */
    paint_delta(seconds, valid, DASH2_DELTA_BEST, now, true);
}

void dashFmtTime(char *out, size_t n, uint32_t ms) {
    snprintf(out, n, "%u:%02u.%03u", (unsigned)(ms / 60000),
             (unsigned)((ms % 60000) / 1000), (unsigned)(ms % 1000));
}

void UiHelper::setLapClock(uint32_t runningMs) {
    static uint32_t shown = 0xFFFFFFFF;
    if (runningMs == shown) return;     /* no fix this frame, or not timing */
    shown = runningMs;

    /* No rate gate, unlike the delta: this is one label on a PARTIAL render
     * path, and a lap clock ticking at 5 Hz reads as broken. */
    char buf[16];
    dashFmtTime(buf, sizeof buf, runningMs);
    ui_dash2_set_lap_clock(buf);
}

/* Finer than the delta number's gate: about a pixel of bar travel. The bar is
 * read peripherally so it should slide rather than step, and it costs one
 * resize on a PARTIAL render path, not a panel repaint. */
#define DELTA_BAR_STEP_S 0.005f

void UiHelper::setDeltaBar(float splitSeconds, bool valid) {
    /* INT32_MIN is the "nothing shown" quantum and so carries the valid flag
     * too: q takes it exactly when !valid, and no reachable split delta
     * quantises near it. */
    static int32_t shown = INT32_MIN;
    int32_t q = valid ? (int32_t)lroundf(splitSeconds / DELTA_BAR_STEP_S) : INT32_MIN;
    if (q == shown) return;
    shown = q;
    ui_dash2_set_delta_bar(splitSeconds, valid);
}

void UiHelper::setPredicted(uint32_t ms, bool valid) {
    static uint32_t shown = 0xFFFFFFFF;
    static uint32_t paintedMs = 0;
    if (!valid) return;                 /* the panel hides it via setLiveDelta */
    uint32_t now = millis();
    if (ms == shown) return;
    /* Same 5 Hz as the delta number it sits beside: seven digits cannot be read
     * faster, and the milliseconds are a blur either way. */
    if ((uint32_t)(now - paintedMs) < DELTA_MIN_REPAINT_MS) return;
    shown = ms; paintedMs = now;

    char buf[16];
    dashFmtTime(buf, sizeof buf, ms);
    ui_dash2_set_predicted(buf);
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

/* ============================================================================
 * SECTOR CELLS
 * Mirrors LapManager's state onto the three cells. Diffed here rather than in
 * ui_dash2, whose setters each trigger a full repaint of the row.
 * ============================================================================ */
void UiHelper::setSectors(int current, uint32_t runningMs, const int64_t *deltaMs,
                          const uint32_t *timeMs, const bool *valid) {
    static int      lastCurrent = -2;
    static int      lastState[3] = { -1, -1, -1 };
    static int32_t  lastVal[3]   = { INT32_MIN, INT32_MIN, INT32_MIN };
    static uint32_t lastSplitTenths = 0xFFFFFFFF;

    /* ONE writer for cell state, including which cell is active: folding
     * ACTIVE into the same computation as the rest is what stops the diff
     * cache and the widget drifting apart. */
    for (int i = 0; i < 3; i++) {
        bool    ok = (valid && valid[i]);
        int64_t d  = ok ? deltaMs[i] : LapManager::LAP_SECTOR_NO_DELTA;
        uint32_t t = (ok && timeMs) ? timeMs[i] : 0;

        /* Driven is ACTIVE and carries its running split; closed with a delta
         * is green or red; closed without one still shows its split; only an
         * unreached or voided sector is blank. */
        dash2_sector_state_t st;
        int32_t val;
        if (i == current) {
            st = DASH2_SECTOR_ACTIVE;  val = 0;
        } else if (!ok || !t) {
            st = DASH2_SECTOR_PENDING; val = 0;
        } else if (d == LapManager::LAP_SECTOR_NO_DELTA) {
            st = DASH2_SECTOR_TIMED;   val = (int32_t)t;
        } else {
            st = (d < 0) ? DASH2_SECTOR_FASTER : DASH2_SECTOR_SLOWER;
            val = (int32_t)d;
        }

        if ((int)st == lastState[i] && val == lastVal[i]) continue;
        lastState[i] = (int)st;
        lastVal[i]   = val;
        ui_dash2_set_sector((dash2_sector_t)i, st, (float)val / 1000.0f);
    }

    if (current != lastCurrent) {
        lastCurrent = current;
        lastSplitTenths = 0xFFFFFFFF;
    }

    /* The running split, in whichever cell is active. Tenths — it only needs
     * to move at a readable rate. */
    if (current >= 0) {
        uint32_t tenths = runningMs / 100;
        if (tenths != lastSplitTenths) {
            lastSplitTenths = tenths;
            char buf[16];
            /* Zero-padded: the Barlow faces have no space glyph, so a leading
             * zero is the only fixed width under ten seconds. */
            snprintf(buf, sizeof(buf), "%02u.%01u",
                     (unsigned)(runningMs / 1000), (unsigned)((runningMs % 1000) / 100));
            ui_dash2_set_running_split(buf);
        }
    }
}

/* ============================================================================
 * GOPRO STATUS CELL
 * Mirrors the SquareLine DISP / GPS cells so the bar stays visually uniform.
 * Four pieces of state, one glance:
 *   text   "--" when the camera is not linked, otherwise its battery %
 *   colour red while recording, battery-graded otherwise
 *   tag    green "CAM" once the camera itself has a GPS lock
 * ============================================================================ */
void UiHelper::build_camera_cell(void) {
    if (!ui_panelstatus || s_cam_var) return;

    lv_obj_t *panel = lv_obj_create(ui_panelstatus);
    lv_obj_set_width (panel, LV_SIZE_CONTENT);
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_align (panel, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(panel, C(T.bg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(panel, 5, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_cam_tag = lv_label_create(panel);
    lv_obj_set_width (s_cam_tag, LV_SIZE_CONTENT);
    lv_obj_set_height(s_cam_tag, LV_SIZE_CONTENT);
    lv_obj_set_align (s_cam_tag, LV_ALIGN_CENTER);
    lv_label_set_text(s_cam_tag, "CAM");
    lv_obj_set_style_text_letter_space(s_cam_tag, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_cam_tag, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_cam_var = lv_label_create(panel);
    lv_obj_set_width (s_cam_var, LV_SIZE_CONTENT);
    lv_obj_set_height(s_cam_var, LV_SIZE_CONTENT);
    lv_obj_set_align (s_cam_var, LV_ALIGN_CENTER);
    lv_label_set_text(s_cam_var, "--");
    lv_obj_set_style_text_font(s_cam_var, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Sit between DISP and GPS rather than after both. */
    lv_obj_move_to_index(panel, 1);

    refresh_camera();
}

void UiHelper::refresh_camera(void) {
    if (!s_cam_var || !s_cam_tag) return;

    lv_obj_set_style_text_color(s_cam_tag,
        C(s_cam_linked && s_cam_gpslock ? T.good : T.muted), 0);

    if (!s_cam_linked) {
        lv_label_set_text(s_cam_var, "--");
        lv_obj_set_style_text_color(s_cam_var, C(T.muted), 0);
        return;
    }

    if (s_cam_batt == 255)
        lv_label_set_text(s_cam_var, s_cam_recording ? "REC" : "??");
    else if (s_cam_recording)
        lv_label_set_text_fmt(s_cam_var, "REC %d%%", s_cam_batt);
    else
        lv_label_set_text_fmt(s_cam_var, "%d%%", s_cam_batt);

    lv_obj_set_style_text_color(s_cam_var,
        C(s_cam_recording ? T.bad : batt_color(s_cam_batt)), 0);
}

void UiHelper::setCamera(bool linked, bool recording, uint8_t battPct, bool gpsLock) {
    if (linked == s_cam_linked && recording == s_cam_recording &&
        battPct == s_cam_batt && gpsLock == s_cam_gpslock) return;   /* no-op */

    s_cam_linked    = linked;
    s_cam_recording = recording;
    s_cam_batt      = battPct;
    s_cam_gpslock   = gpsLock;
    refresh_camera();
}

/* ============================================================================
 * WIFI PORTAL — status cell + config-screen toggle
 * Both built at runtime so the SquareLine project stays untouched. The cell is
 * hidden entirely while the portal is off: a SoftAP costs ~100 mA, so its
 * presence on the bar should mean something.
 * ============================================================================ */
extern "C" void ui_helper_toggle_wifi(void);   /* implemented in main_display */
extern "C" void ui_helper_open_sessions(void); /* implemented in main_display */
extern "C" void ui_helper_open_logs(void);     /* implemented in main_display */

static void wifi_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) ui_helper_toggle_wifi();
}
static void sessions_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) ui_helper_open_sessions();
}
static void logs_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) ui_helper_open_logs();
}

void UiHelper::build_wifi_cell(void) {
    if (!ui_panelstatus || s_wifi_var) return;

    s_wifi_panel = lv_obj_create(ui_panelstatus);
    lv_obj_set_width (s_wifi_panel, LV_SIZE_CONTENT);
    lv_obj_set_height(s_wifi_panel, LV_SIZE_CONTENT);
    lv_obj_set_align (s_wifi_panel, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(s_wifi_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_wifi_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(s_wifi_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_wifi_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_wifi_panel, C(T.bg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_wifi_panel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_wifi_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(s_wifi_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(s_wifi_panel, 5, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_wifi_var = lv_label_create(s_wifi_panel);
    lv_obj_set_width (s_wifi_var, LV_SIZE_CONTENT);
    lv_obj_set_height(s_wifi_var, LV_SIZE_CONTENT);
    lv_obj_set_align (s_wifi_var, LV_ALIGN_CENTER);
    lv_label_set_text(s_wifi_var, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(s_wifi_var, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_move_to_index(s_wifi_panel, 2);
    refresh_wifi();
}

/* Shared styling for the runtime-added config-screen buttons, so they are
 * indistinguishable from the SquareLine ones. */
lv_obj_t *UiHelper::make_setup_button(const char *text, lv_event_cb_t cb,
                                      lv_obj_t **out_label) {
    lv_obj_t *btn = lv_button_create(ui_panelsetupbuttons);
    lv_obj_set_width (btn, 250);
    lv_obj_set_height(btn, 50);
    lv_obj_set_align (btn, LV_ALIGN_TOP_MID);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_ext_click_area(btn, 5);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(btn, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, C(T.surface2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, C(T.muted), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    /* Fixed height + no shrink: inside a scrolling flex column the buttons
     * would otherwise be squeezed to fit rather than overflow into a scroll. */
    lv_obj_set_style_flex_grow(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_width (lbl, LV_SIZE_CONTENT);
    lv_obj_set_height(lbl, LV_SIZE_CONTENT);
    lv_obj_set_align (lbl, LV_ALIGN_CENTER);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, C(T.fg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    if (out_label) *out_label = lbl;
    return btn;
}

void UiHelper::build_wifi_button(void) {
    if (!ui_panelsetupbuttons || s_wifi_btn_lbl) return;

    /* The panel was built non-scrollable for its original two buttons. With
     * SESSIONS and WIFI PORTAL added it overflows, so let it scroll rather
     * than pushing the last button off the bottom where it cannot be reached. */
    lv_obj_add_flag(ui_panelsetupbuttons, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui_panelsetupbuttons, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ui_panelsetupbuttons, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_align(ui_panelsetupbuttons, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(ui_panelsetupbuttons, 12, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* SESSIONS sits above the portal button — reviewing a run is the more
     * common reason to come here than moving files off the card. */
    make_setup_button("SESSIONS", sessions_btn_cb, nullptr);
    make_setup_button("SYSTEM LOG", logs_btn_cb, nullptr);
    make_setup_button("WIFI PORTAL: OFF", wifi_btn_cb, &s_wifi_btn_lbl);
    refresh_wifi();
}

/* ============================================================================
 * ALERT BANNER — dashboard overlay, tap to open the log
 * Injected onto ui_dashboardscreen after the dash2 swap, so it is the last
 * child and paints above the dashboard. Hidden unless something has actually
 * gone wrong: a banner that is always there stops being read.
 * Bottom edge, because speed and lap times own the top of this screen.
 * ============================================================================ */
static lv_obj_t *s_alert     = nullptr;
static lv_obj_t *s_alert_lbl = nullptr;

void UiHelper::build_alert_banner(void) {
    if (!ui_dashboardscreen || s_alert) return;

    s_alert = lv_obj_create(ui_dashboardscreen);
    lv_obj_set_size(s_alert, LV_PCT(94), 32);
    lv_obj_align(s_alert, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_remove_flag(s_alert, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_alert, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_alert, C(T.bad_deep), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_alert, 235, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_alert, C(T.bad), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_alert, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(s_alert, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_alert, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_flag(s_alert, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_alert, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_alert, 10);
    lv_obj_add_event_cb(s_alert, logs_btn_cb, LV_EVENT_CLICKED, NULL);

    s_alert_lbl = lv_label_create(s_alert);
    lv_obj_center(s_alert_lbl);
    lv_label_set_text(s_alert_lbl, "");
    lv_obj_set_style_text_color(s_alert_lbl, C(T.fg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_alert_lbl, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void UiHelper::setAlert(uint16_t errors, uint16_t warnings) {
    if (!s_alert) return;

    if (errors == 0 && warnings == 0) {
        lv_obj_add_flag(s_alert, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* Errors dominate the text when present — a warning count next to a real
     * failure buries the thing that matters. */
    if (errors) lv_label_set_text_fmt(s_alert_lbl, "%u ERROR%s  -  TAP FOR LOG",
                                      (unsigned)errors, errors == 1 ? "" : "S");
    else        lv_label_set_text_fmt(s_alert_lbl, "%u WARNING%s  -  TAP FOR LOG",
                                      (unsigned)warnings, warnings == 1 ? "" : "S");

    lv_obj_set_style_border_color(s_alert, C(errors ? T.bad : T.accent),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(s_alert, LV_OBJ_FLAG_HIDDEN);
}

void UiHelper::refresh_wifi(void) {
    if (s_wifi_panel) {
        if (s_wifi_running) lv_obj_remove_flag(s_wifi_panel, LV_OBJ_FLAG_HIDDEN);
        else                lv_obj_add_flag(s_wifi_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_wifi_var) {
        /* A connected client is the interesting state — it means someone is
         * actually pulling files, so don't cut the radio. */
        if (s_wifi_clients > 0)
            lv_label_set_text_fmt(s_wifi_var, LV_SYMBOL_WIFI " %d", s_wifi_clients);
        else
            lv_label_set_text(s_wifi_var, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(s_wifi_var,
            C(s_wifi_clients > 0 ? T.good : T.accent), 0);
    }
    if (s_wifi_btn_lbl) {
        if (s_wifi_running && s_wifi_ip[0])
            lv_label_set_text_fmt(s_wifi_btn_lbl, "WIFI PORTAL: %s", s_wifi_ip);
        else
            lv_label_set_text(s_wifi_btn_lbl, s_wifi_running ? "WIFI PORTAL: ON" : "WIFI PORTAL: OFF");
        lv_obj_set_style_text_color(s_wifi_btn_lbl, C(s_wifi_running ? T.accent : T.fg), 0);
    }
}

/* A portal that refuses to start must say so — silently staying on OFF looks
 * exactly like a dead button, which is how this went unnoticed the first time. */
void UiHelper::setWifiError(void) {
    s_wifi_running = false;
    s_wifi_clients = 0;
    s_wifi_ip[0]   = '\0';
    refresh_wifi();
    if (s_wifi_btn_lbl) {
        lv_label_set_text(s_wifi_btn_lbl, "WIFI PORTAL: FAILED");
        lv_obj_set_style_text_color(s_wifi_btn_lbl, C(T.bad), 0);
    }
}

void UiHelper::setWifi(bool running, uint8_t clients, const char *ip) {
    bool same = (running == s_wifi_running) && (clients == s_wifi_clients) &&
                (ip ? strncmp(ip, s_wifi_ip, sizeof(s_wifi_ip)) == 0 : s_wifi_ip[0] == '\0');
    if (same) return;

    s_wifi_running = running;
    s_wifi_clients = clients;
    if (ip) { strncpy(s_wifi_ip, ip, sizeof(s_wifi_ip) - 1); s_wifi_ip[sizeof(s_wifi_ip) - 1] = '\0'; }
    else    { s_wifi_ip[0] = '\0'; }
    refresh_wifi();
}

/* ============================================================================
 * SPLIT-GATE ROWS (track setup)
 * Two more gates below START LINE, built at runtime so the SquareLine file
 * stays untouched. Styling is copied from ui_panellinel rather than
 * approximated — same 32 px height, same surface, rule border, radius and
 * padding — so the four new rows are indistinguishable from the two above.
 * ============================================================================ */
extern "C" void ui_helper_pin_sector(int gate, int side);   /* ui_theme.cpp */
extern "C" void ui_helper_edit_sector(int gate, int side, int is_lat);

/* Tapping a coordinate (or "-- not set --") types it, exactly as the START
 * LINE rows do. user_data packs gate/side/is_lat. */
static void gate_edit_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    intptr_t t = (intptr_t)lv_event_get_user_data(e);
    ui_helper_edit_sector((int)(t >> 2), (int)((t >> 1) & 1), (int)(t & 1));
}

/* Make a coordinate label behave like the generated ones: clickable, with a
 * hit area tall enough to catch a gloved finger on a 32 px row. */
static void gate_make_editable(lv_obj_t *lbl, int gate, int side, int is_lat) {
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(lbl, 10);
    lv_obj_add_event_cb(lbl, gate_edit_cb, LV_EVENT_CLICKED,
                        (void *)(intptr_t)((gate << 2) | (side << 1) | is_lat));
}

UiHelper::GateRow UiHelper::s_gate[2][2] = {};
bool UiHelper::s_gate_built = false;

static void gate_pin_cb(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    intptr_t tag = (intptr_t)lv_event_get_user_data(e);
    ui_helper_pin_sector((int)(tag >> 1), (int)(tag & 1));
}

/* Section caption in the same style as the START LINE header. */
static lv_obj_t *gate_header(lv_obj_t *parent, const char *title, const char *hint) {
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_width(p, lv_pct(100));
    lv_obj_set_height(p, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(p, C(T.bg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(p, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(p, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(p, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(p, 6, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *t = lv_label_create(p);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, C(T.fg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *h = lv_label_create(p);
    lv_label_set_text(h, hint);
    lv_obj_set_style_text_color(h, C(T.muted), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(h, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    return p;
}

void UiHelper::build_sector_rows(void) {
    if (!ui_panelcoord || s_gate_built) return;
    s_gate_built = true;

    /* The body was sized for two coordinate rows. With six it overflows, so it
     * has to scroll or the SAVE bar becomes unreachable.
     *
     * It also has to be shortened. SquareLine gave it y=66 h=254, which runs to
     * y=320 — straight under the action bar at y=272. Nothing noticed while the
     * content was short, but once it scrolls the last 48 px can never be
     * reached: scrolling stops at the content end, which is still behind the
     * bar, and the elastic bounce springs it back. Padding does not fix that —
     * the container itself must stop where the bar starts. */
    if (ui_panelbody) {
        const int ACTION_BAR_Y = 272, BODY_Y = 66;
        lv_obj_set_height(ui_panelbody, ACTION_BAR_Y - BODY_Y);
        lv_obj_add_flag(ui_panelbody, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(ui_panelbody, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(ui_panelbody, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_pad_bottom(ui_panelbody, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    static const char *TITLE[2] = { "SECTOR 1", "SECTOR 2" };
    static const char *HINT[2]  = { "closes S1", "closes S2" };

    for (int g = 0; g < 2; g++) {
        gate_header(ui_panelcoord, TITLE[g], HINT[g]);

        for (int s = 0; s < 2; s++) {
            GateRow &row = s_gate[g][s];

            lv_obj_t *p = lv_obj_create(ui_panelcoord);
            lv_obj_set_height(p, 32);
            lv_obj_set_width(p, lv_pct(100));
            lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(p, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(p, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(p, C(T.surface), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(p, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(p, C(T.rule), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_opa(p, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(p, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_left(p, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_right(p, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_top(p, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_bottom(p, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_column(p, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

            lv_obj_t *tag = lv_label_create(p);
            lv_label_set_text(tag, s == SETUP_LINE_L ? "L" : "R");
            lv_obj_set_style_text_color(tag, C(T.accent), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(tag, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

            row.lat = lv_label_create(p);
            lv_label_set_text(row.lat, "");
            lv_obj_set_style_text_color(row.lat, C(T.fg), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(row.lat, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

            row.lon = lv_label_create(p);
            lv_label_set_text(row.lon, "");
            lv_obj_set_style_text_color(row.lon, C(T.fg), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(row.lon, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

            row.empty = lv_label_create(p);
            lv_label_set_text(row.empty, "-- not set --");
            lv_obj_set_style_text_color(row.empty, C(T.muted), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(row.empty, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

            lv_obj_t *btn = lv_button_create(p);
            lv_obj_set_width(btn, 64);
            lv_obj_set_height(btn, 24);
            lv_obj_set_ext_click_area(btn, 5);
            lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_radius(btn, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(btn, C(T.surface2), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(btn, C(T.rule), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(btn, C(T.accent), LV_PART_MAIN | LV_STATE_PRESSED);
            lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_PRESSED);
            lv_obj_add_event_cb(btn, gate_pin_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)((g << 1) | s));

            row.pin_lbl = lv_label_create(btn);
            lv_label_set_text(row.pin_lbl, "PIN");
            lv_obj_center(row.pin_lbl);
            lv_obj_set_style_text_color(row.pin_lbl, C(T.fg), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_letter_space(row.pin_lbl, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(row.pin_lbl, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);

            gate_make_editable(row.lat,   g, s, 1);
            gate_make_editable(row.lon,   g, s, 0);
            gate_make_editable(row.empty, g, s, 1);   /* same as the L/R rows */

            row.c = { 0, 0, false };
            refresh_sector_row(g, (setup_line_side_t)s);
        }
    }
}

void UiHelper::refresh_sector_row(int gate, setup_line_side_t side) {
    if (gate < 0 || gate > 1) return;
    GateRow &row = s_gate[gate][side];
    if (!row.lat) return;

    if (row.c.valid) {
        char buf[24];
        snprintf(buf, sizeof(buf), "LAT %.4f%c", fabs(row.c.lat), row.c.lat >= 0 ? 'N' : 'S');
        lv_label_set_text(row.lat, buf);
        snprintf(buf, sizeof(buf), "LON %.4f%c", fabs(row.c.lon), row.c.lon >= 0 ? 'E' : 'W');
        lv_label_set_text(row.lon, buf);
        lv_obj_remove_flag(row.lat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(row.lon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(row.empty, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(row.pin_lbl, "RESET");
    } else {
        lv_obj_add_flag(row.lat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(row.lon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(row.empty, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(row.pin_lbl, "PIN");
    }
}

void UiHelper::setSectorCoord(int gate, setup_line_side_t side,
                              double lat, double lon, bool valid) {
    if (gate < 0 || gate > 1) return;
    s_gate[gate][side].c = { lat, lon, valid };
    refresh_sector_row(gate, side);
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
    build_sector_rows();
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

    /* ----- Dashboard (v2 owns its own tokens) ----- */
    ui_dash2_set_mode(mode == DASH_MODE_DAY ? DASH2_MODE_DAY : DASH2_MODE_NIGHT);

    /* ----- Runtime-built cells (GoPro, WiFi portal) ----- */
    refresh_camera();
    refresh_wifi();

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
/* ============================================================================
 * RECORDING INDICATOR
 * The v2 design has no recording affordance, but knowing at a glance whether
 * the session is actually logging matters more mid-race than anything else on
 * the screen. It lives in the status bar rather than on the dashboard so it is
 * visible from the config, track and sessions screens too — you can wander
 * into a menu mid-session and still see that you are recording.
 * ============================================================================ */
extern "C" void ui_helper_stop_session(void);   /* implemented in main_display */

static void rec_cell_cb(lv_event_t *e) {
    /* Tap to stop. The cell doubles as the DEMO indicator, so it stops
     * whichever is running; both calls no-op when their mode is not. */
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ui_helper_exit_demo_mode();
    ui_helper_stop_session();
}

void UiHelper::build_rec_cell(void) {
    if (!ui_panelstatus || s_rec_panel) return;

    s_rec_panel = lv_obj_create(ui_panelstatus);
    lv_obj_set_width (s_rec_panel, LV_SIZE_CONTENT);
    lv_obj_set_height(s_rec_panel, LV_SIZE_CONTENT);
    lv_obj_set_align (s_rec_panel, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(s_rec_panel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_rec_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(s_rec_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_rec_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(s_rec_panel, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    /* Generous hit area — this gets pressed with gloves on. */
    lv_obj_set_ext_click_area(s_rec_panel, 8);
    lv_obj_add_flag(s_rec_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_rec_panel, rec_cell_cb, LV_EVENT_CLICKED, NULL);

    s_rec_dot = lv_obj_create(s_rec_panel);
    lv_obj_remove_style_all(s_rec_dot);
    lv_obj_set_size(s_rec_dot, 10, 10);
    lv_obj_set_style_radius(s_rec_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_rec_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_rec_dot, C(T.bad), 0);

    s_rec_lbl = lv_label_create(s_rec_panel);
    lv_label_set_text(s_rec_lbl, "REC");
    lv_obj_set_style_text_letter_space(s_rec_lbl, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_rec_lbl, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_rec_lbl, C(T.bad), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Leftmost, so it is the first thing in the eye's path across the bar. */
    lv_obj_move_to_index(s_rec_panel, 0);
    lv_obj_add_flag(s_rec_panel, LV_OBJ_FLAG_HIDDEN);
}

static void rec_blink_cb(void *obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

void UiHelper::set_rec_cell(rec_cell_t mode) {
    if (!s_rec_panel || mode == s_rec_mode) return;
    s_rec_mode = mode;

    if (mode == REC_OFF) {
        lv_anim_delete(s_rec_dot, rec_blink_cb);
        lv_obj_set_style_opa(s_rec_dot, LV_OPA_COVER, 0);
        lv_obj_add_flag(s_rec_panel, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    /* Accent, not red: red on this bar means "being written to the card". */
    uint32_t col = (mode == REC_DEMO) ? T.accent : T.bad;
    lv_label_set_text(s_rec_lbl, (mode == REC_DEMO) ? "DEMO" : "REC");
    lv_obj_set_style_text_color(s_rec_lbl, C(col), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_rec_dot, C(col), 0);
    lv_obj_remove_flag(s_rec_panel, LV_OBJ_FLAG_HIDDEN);

    /* Restarted rather than left running, so switching mode cannot leave the
     * dot mid-fade at the old colour. Via LVGL's animator rather than from
     * loop(): every style write here costs a full-frame QSPI blit. */
    lv_anim_delete(s_rec_dot, rec_blink_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_rec_dot);
    lv_anim_set_exec_cb(&a, rec_blink_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_20);
    lv_anim_set_duration(&a, 450);
    lv_anim_set_playback_duration(&a, 450);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

void UiHelper::setSessionState(bool active) {
    if (ui_labelstartsession)
        lv_label_set_text(ui_labelstartsession, active ? "STOP SESSION" : "START SESSION");
    set_rec_cell(active ? REC_SESSION : REC_OFF);
}

void UiHelper::setDemoState(bool on) {
    /* Never clobbers a live REC: a session and a demo cannot both be running,
     * and if they somehow were, the recording is the one that must show. */
    if (!on && s_rec_mode != REC_DEMO) return;
    set_rec_cell(on ? REC_DEMO : REC_OFF);
}

/* Kept so the call site in loop() needs no #ifdef. The blink is an LVGL
 * animation now, so there is nothing to tick. */
void UiHelper::tickRecordingPanel() {}

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
extern "C" void ui_helper_set_sector_coord(int gate, int side, double lat, double lon, bool valid) {
    if (s_instance) s_instance->setSectorCoord(gate, (setup_line_side_t)side, lat, lon, valid);
}
extern "C" void ui_helper_set_start_r(double lat, double lon, bool valid) {
    if (s_instance) s_instance->setStartR(lat, lon, valid);
}
extern "C" void ui_helper_set_track_names(const char *const *names, int n) {
    if (s_instance) s_instance->setTracks(names, n);
}