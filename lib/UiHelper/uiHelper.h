#ifndef UI_HELPER_H
#define UI_HELPER_H

#include <Arduino.h>
#include "esp_bsp.h"
#include "ui.h"
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

class UiHelper {
public:
    void init();
    void setSpeed(int kmh);
    void setGx(float gx);                              /* lateral, ±2g */
    void setGy(float gy);                              /* longitudinal, ±2g */
    void setLap(uint8_t lap_num,
                const char *lap_str,                   /* e.g. "1:23.74" */
                const char *best_str);                 /* e.g. "1:23.32" */
    void setDelta(float seconds, bool faster);         /* faster = green pill */
    void hideHelmet();                                 /* hide helmet from statusbar */
    void setHelmet(uint8_t pct);                       /* battery level */ 
    void setDisplay(uint8_t pct);                      /* battery level */
    void setGps(uint8_t pct);                          /* number of satellites */
    void setPps(uint8_t expected_pps, uint8_t pct);    /* number of packets per second */
    void setTheme(dash_mode_t mode);                   /* day / night swap */
    void setSessionState(bool active);                 /* updates button label + recording panel */
    void tickRecordingPanel();                         /* call every frame to drive the blink */
    void setTracks(const char *const *names, int count);
    void setTrackIdx(int idx);
    int  getTrackIdx(void) { return s_track_idx; };
    void setStartL(double lat, double lon, bool valid);
    void setStartR(double lat, double lon, bool valid);
    void setDirty(bool dirty);

private:
    static uint32_t batt_color(uint8_t pct);
    static uint32_t gps_color(uint8_t n);
    static uint32_t esp_color(uint8_t expected_pps, uint8_t pps);
    static void refresh_track_name(void);
    static void refresh_coord_row(setup_line_side_t side);
    static void refresh_dirty(void);

    static const char *s_track_names[SETUP_MAX_TRACKS];
    static int        s_track_count;
    static int        s_track_idx;
    static bool       s_dirty;

    static setup_coord_t s_line_l;
    static setup_coord_t s_line_r;
};

#endif // UI_HELPER_H