#ifndef UI_HELPER_H
#define UI_HELPER_H

#include <Arduino.h>
#include "esp_bsp.h"
#include "ui.h"
#include "ui_dash2.h"     /* the v2 dashboard replaces SquareLine's */
#include "LapManager.h"   /* LAP_SECTOR_NO_DELTA for the sector band */
#include "display.h"
#include "lv_port.h"

#define SETUP_MAX_TRACKS 16

typedef enum {
    DASH_MODE_NIGHT = 0,    /* black bg, amber accent (default) */
    DASH_MODE_DAY   = 1,    /* black bg, cyan accent for daylight LCD pop */
} dash_mode_t;

typedef struct {
    uint32_t bg, surface, surface2, fg, fg2, muted, rule, accent, accent_fg, good, bad, good_deep, bad_deep;
} dash_theme_t;

typedef struct { double lat, lon; bool valid; } setup_coord_t;

/* Side of the start-line — passed back to the pin callback. */
typedef enum {
    SETUP_LINE_L = 0,
    SETUP_LINE_R = 1,
} setup_line_side_t;

static const dash_theme_t THEME_NIGHT = {
    .bg = 0x050608, .surface = 0x0D1014, .surface2 = 0x14181E, .fg = 0xF6F8FB,
    .fg2 = 0xCBD0D8, .muted = 0x6B7280, .rule = 0x1C2026, .accent = 0xFFD400,
    .accent_fg = 0x1A1500, .good = 0x2EE07A, .bad = 0xFF3B3B,
    .good_deep = 0x0F3A23, .bad_deep = 0x3A0F10
};
static const dash_theme_t THEME_DAY = {
    .bg = 0xF4F5F7, .surface = 0xFFFFFF, .surface2 = 0xECEEF2, .fg = 0x14161A,
    .fg2 = 0x3A3F47, .muted = 0x9AA3AF, .rule = 0xD8DBE1, .accent = 0xFF9500,
    .accent_fg = 0x1A0E00, .good = 0x29FF8A, .bad = 0xFF5050,
    .good_deep = 0x0A5933, .bad_deep = 0x3A2425
};

/* Lap/clock times on the dash, always m:ss.mmm. Dropping the "0:" under a
 * minute reflows the string as a time crosses sixty seconds, which is the
 * jitter the tabular faces exist to remove; lib/DashV2's widths are budgeted
 * for this form. */
void dashFmtTime(char *out, size_t n, uint32_t ms);

class UiHelper {
public:
    void init();
    void setSpeed(int kmh);
    void setGx(float gx);                              /* lateral, ±2g */
    void setGy(float gy);                              /* longitudinal, ±2g */
    void setLap(uint8_t lap_num,
                const char *lap_str,                   /* e.g. "1:23.74" */
                const char *best_str);                 /* e.g. "1:23.32" */
    /* Live lap delta. Called every frame; `valid` false blanks it, which is
     * what to show before a reference lap exists. Self-filtering, because
     * ui_dash2_set_delta repaints the panel and four labels. Ignored while a
     * best-lap flash is holding. */
    void setLiveDelta(float seconds, bool valid);

    /* Latch the purple session-best state at the line for a few seconds, then
     * hand back to the live delta. `valid` false on the first timed lap. */
    void flashBestLap(float seconds, bool valid);
    void setDisplay(uint8_t pct);                      /* battery level */

    /* --- Charge mode --- */
    void showChargeScreen();                           /* swap to the charge screen, hide status bar */
    void hideChargeScreen();                           /* return to the dashboard */
    void setChargeBattery(uint8_t pct, float volts);   /* refresh the charge readout */
    void setGps(uint8_t pct);                          /* number of satellites */
    void setCamera(bool linked, bool recording,        /* GoPro status cell */
                   uint8_t battPct, bool gpsLock);
    void setWifi(bool running, uint8_t clients,        /* WiFi portal cell  */
                 const char *ip);
    void setWifiError(void);                           /* portal failed to start */

    /* Lap clock, bottom-left. */
    void setLapClock(uint32_t runningMs);

    /* Split-relative, not the cumulative lap delta — see LapManager's
     * virtual-splits block. Gated finer than the number beside it: this is
     * read without looking, and costs one resize, not a panel repaint. */
    void setDeltaBar(float splitSeconds, bool valid);

    /* Predicted lap time: the reference lap plus the delta being carried. */
    void setPredicted(uint32_t ms, bool valid);

    /* Sector cells. `current` is the sector being driven (-1 none), `runningMs`
     * its elapsed time. For each closed sector pass its delta vs its own
     * previous best in `deltaMs` (or LAP_SECTOR_NO_DELTA) AND its split in
     * `timeMs` — a sector that closes without a comparison still has a time,
     * and showing it is what stops the cell blanking. Self-filtering. */
    void setSectors(int current, uint32_t runningMs, const int64_t *deltaMs,
                    const uint32_t *timeMs, const bool *valid);
    void setTheme(dash_mode_t mode);                   /* day / night swap */
    void setSessionState(bool active);                 /* updates button label + recording panel */
    void tickRecordingPanel();                         /* call every frame to drive the blink */
    void setTracks(const char *const *names, int count);
    void setTrackIdx(int idx);
    int  getTrackIdx(void) { return s_track_idx; };
    void setStartL(double lat, double lon, bool valid);
    void setStartR(double lat, double lon, bool valid);

    /* Split gates on the track setup screen. gate 0 = S1, 1 = S2. */
    void setSectorCoord(int gate, setup_line_side_t side,
                        double lat, double lon, bool valid);

    void setDirty(bool dirty);

    /* Dashboard alert banner. Hidden at zero; tapping it opens the log screen. */
    void setAlert(uint16_t errors, uint16_t warnings);

private:
    static void paint_delta(float seconds, bool valid,
                            dash2_delta_state_t st, uint32_t now, bool force);

    static void build_alert_banner(void);
    static uint32_t batt_color(uint8_t pct);
    static uint32_t gps_color(uint8_t n);
    static void build_camera_cell(void);
    static void refresh_camera(void);
    static void build_rec_cell(void);
    static void build_wifi_cell(void);
    static void build_wifi_button(void);
    static void refresh_wifi(void);
    static lv_obj_t *make_setup_button(const char *text, lv_event_cb_t cb,
                                       lv_obj_t **out_label);
    static void build_charge_screen(void);             /* hand-built; not a SquareLine export */
    static void build_charge_mode_button(void);        /* injected into the config screen at runtime */
    static void build_version_label(void);             /* ditto — FW_VERSION in the setup header */
    static void refresh_track_name(void);
    static void refresh_coord_row(setup_line_side_t side);
    static void refresh_dirty(void);

    /* Split-gate rows, built at runtime onto the SquareLine track screen so the
     * generated file stays untouched. Same widget tree and styling as the
     * START LINE rows above them. */
    static void build_sector_rows(void);
    static void refresh_sector_row(int gate, setup_line_side_t side);
    struct GateRow { lv_obj_t *lat, *lon, *empty, *pin_lbl; setup_coord_t c; };
    static GateRow s_gate[2][2];      /* [S1|S2][L|R] */
    static bool    s_gate_built;

    static const char *s_track_names[SETUP_MAX_TRACKS];
    static int        s_track_count;
    static int        s_track_idx;
    static bool       s_dirty;

    static setup_coord_t s_line_l;
    static setup_coord_t s_line_r;

    /* GoPro status cell — built at runtime onto the SquareLine status bar. */
    static lv_obj_t *s_cam_tag;
    static lv_obj_t *s_cam_var;
    static bool      s_cam_linked;
    static bool      s_cam_recording;
    static bool      s_cam_gpslock;
    static uint8_t   s_cam_batt;

    /* WiFi portal cell + config-screen toggle, also built at runtime. */
    static lv_obj_t *s_rec_panel;
    static lv_obj_t *s_rec_dot;
    static lv_obj_t *s_rec_lbl;
    static bool      s_rec_active;
    static lv_obj_t *s_wifi_panel;
    static lv_obj_t *s_wifi_var;
    static lv_obj_t *s_wifi_btn_lbl;
    static bool      s_wifi_running;
    static uint8_t   s_wifi_clients;
    static char      s_wifi_ip[20];

    /* Charge mode — a hand-built parking screen, see uiHelper.cpp. */
    static lv_obj_t *s_charge_screen;
    static lv_obj_t *s_charge_pct;
    static lv_obj_t *s_charge_volts;
};

#endif // UI_HELPER_H