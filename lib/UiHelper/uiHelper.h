#ifndef UI_HELPER_H
#define UI_HELPER_H

#include <Arduino.h>
#include "esp_bsp.h"
#include "ui.h"
#include "display.h"
#include "lv_port.h"

typedef enum {
    DASH_MODE_NIGHT = 0,    /* black bg, amber accent (default) */
    DASH_MODE_DAY   = 1,    /* black bg, cyan accent for daylight LCD pop */
} dash_mode_t;

typedef struct {
    uint32_t bg, fg, muted, track, accent, good, bad, good_deep, bad_deep;
} dash_theme_t;

static const dash_theme_t THEME_NIGHT = {
    .bg = 0x050608, .fg = 0xF6F8FB, .muted = 0x6B7280,
    .track = 0x1A1D23, .accent = 0xFFD400,           /* amber */
    .good = 0x2EE07A, .bad = 0xFF3B3B,
    .good_deep = 0x0F3A23, .bad_deep = 0x3A0F10,
};
static const dash_theme_t THEME_DAY = {
    .bg = 0x050608, .fg = 0xFFFFFF, .muted = 0x9AA3AF,
    .track = 0x20242C, .accent = 0x00E5FF,           /* cyan */
    .good = 0x29FF8A, .bad = 0xFF5050,
    .good_deep = 0x0F3A23, .bad_deep = 0x3A0F10,
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
    void setHelmet(uint8_t pct);                       /* battery level */ 
    void setDisplay(uint8_t pct);                      /* battery level */
    void setGps(uint8_t pct);                          /* number of satellites */
    void setPps(uint8_t expected_pps, uint8_t pct);    /* number of packets per second */
    void setTheme(dash_mode_t mode);                   /* day / night swap */
    void setSessionState(bool active);                 /* updates button label + recording panel */
    void tickRecordingPanel();                         /* call every frame to drive the blink */

private:
    static uint32_t batt_color(uint8_t pct);
    static uint32_t gps_color(uint8_t n);
    static uint32_t esp_color(uint8_t expected_pps, uint8_t pps);
};

#endif // UI_HELPER_H