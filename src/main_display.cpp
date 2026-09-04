#include <Arduino.h>
#include "esp_task_wdt.h"   // display-wedge safety net (used throughout)
#include "esp_system.h"     // esp_reset_reason

// QSPI flush telemetry from lib/DisplayBSP/lv_port.c (C linkage). Reported once a
// second below via log_w so it surfaces at CORE_DEBUG_LEVEL=WARN.
extern "C" {
    extern volatile uint32_t lvgl_port_frame_drops;
    extern volatile uint32_t lvgl_port_max_stall_us;
}

// Library includes from our new /lib folder
#include "LogManager.h"
#include "EspNowProtocol.h"
#include "uiHelper.h"
#include "BatteryManager.h"
#include "ConfigManager.h"
#include "LapManager.h"
#include "DemoTrack.h"
#include "GpsManager.h"

#if defined(ENABLE_GOPRO)
#include "GoProManager.h"
#endif

#if defined(ENABLE_WEB_PORTAL)
#include "WebPortal.h"
#endif

#include "SessionBrowser.h"
#include "LogBuffer.h"
#include "LogScreen.h"

// Opens the on-device session review screens. Called from the config screen,
// already in LVGL context — the browser loads a placeholder and repaints before
// the (slow) SD read, so the press feels immediate.
extern "C" void ui_helper_open_sessions(void) {
    sessionBrowser.openList();
}

// Opens the log screen. Reached from the config screen and from the dashboard
// alert banner. Renders out of LogBuffer's RAM ring, so unlike the session
// browser there is no slow read to defer — it can run in LVGL context.
extern "C" void ui_helper_open_logs(void) {
    logScreen.open();
}

#if defined(ENABLE_IMU)
#include "ImuManager.h"
#include "CalibrationManager.h"

#define I2C_SDA 18
#define I2C_SCL 17
#endif

const uint8_t timeBetweenImuReads = 10; // ms between IMU reads (100 Hz)

// --- GLOBAL STATE ---
LogManager logManager;
UiHelper   uiHelper;
LapManager lapManager;

GpsManager gps(GPS_STANDALONE_RX, GPS_STANDALONE_TX);

#if defined(ENABLE_GOPRO)
GoProManager goPro;
#define GOPRO_UI_INTERVAL_MS 500
static uint32_t lastGoProUiMs = 0;
#endif

#if defined(ENABLE_WEB_PORTAL)
WebPortal webPortal;
#define WEB_PORTAL_UI_INTERVAL_MS 1000
static uint32_t lastWebUiMs = 0;

// Called from the config-screen button (LVGL context). start() brings up the
// SoftAP and HTTP server, which takes a moment — acceptable here because the
// user just pressed a button and the portal task does the serving.
extern "C" void ui_helper_toggle_wifi(void) {
    if (webPortal.isRunning()) {
        webPortal.stop();
#if defined(ENABLE_GOPRO)
        goPro.resumeRadio();
#endif
        uiHelper.setWifi(false, 0, nullptr);
        return;
    }

    // WiFi needs ~50 kB of internal DRAM and cannot use PSRAM; NimBLE is
    // holding a comparable amount, so the BLE stack stands down first.
    // Otherwise esp_wifi_init fails with ESP_ERR_NO_MEM.
#if defined(ENABLE_GOPRO)
    goPro.suspendRadio();
#endif
    bool up = webPortal.start();
    if (!up) {
#if defined(ENABLE_GOPRO)
        goPro.resumeRadio();      // give it back, we did not need it after all
#endif
        uiHelper.setWifiError();  // say so on the button rather than sit silent
        return;
    }
    String wip = webPortal.ip();
    uiHelper.setWifi(true, webPortal.clients(), wip.c_str());
}
#else
extern "C" void ui_helper_toggle_wifi(void) {}
#endif

// Latest assembled telemetry frame. Written by loop() from the local GPS (plus
// IMU when enabled), consumed by syncUI() for the dashboard and the SD log.
static TelemetryMsg telemetry = {};
static bool newTelemetryAvailable = false;

#if defined(ENABLE_IMU)
ImuManager imu;
CalibrationManager calibManager(imu);
ImuData latestImuData = {0};
bool imuReady = false;
uint32_t lastImuReadMs = 0;
#endif

// Display battery: BAT+ → 68k (683) → IO5 → 100k (01D) → GND (always-on divider)
// Theoretical ratio is 1.68; the ADC reads low (~40k source impedance starves its
// sample-and-hold), so it is fitted to a multimeter instead. To re-fit: scale by
// meter/reported, on battery only — with USB in the node is the charger rail.
// ponytail: gain-only fit, exact at ~4.0V. If it reads high near 3.5V, take a point
// down there and go two-point (gain + offset).
#define DISPLAY_BATT_ADC 5
#define DISPLAY_BATT_READ_INTERVAL_MS 5000
BatteryManager displayBattery(DISPLAY_BATT_ADC, 0xFF, 1.7269f);
static uint32_t lastDisplayBattReadMs = 0;

// Smoothing & Metrics
float displaySpeed = 0;
float targetSpeed = 0;
const float lerpFactor = 0.20f;
static uint32_t lastHealthReportMs = 0;

// Lap state — updated inside syncUI()
static uint8_t s_lapCount = 0;

/* Reference-trace storage for the live lap delta. LapManager allocates
 * nothing itself, so the buffers are handed to it from here, in PSRAM next to
 * the log ring rather than the scarce internal heap. */
#define LAP_TRACE_POINTS LapManager::kRecommendedTracePoints
static LapTracePoint *s_traceRef = nullptr;
static LapTracePoint *s_traceCur = nullptr;

static void initLapTrace() {
    size_t bytes = sizeof(LapTracePoint) * LAP_TRACE_POINTS;
    s_traceRef = (LapTracePoint *)ps_malloc(bytes);
    s_traceCur = (LapTracePoint *)ps_malloc(bytes);
    if (!s_traceRef || !s_traceCur) {
        /* No fallback to the internal heap: 24 KB there is worth more than a
         * live delta, and LapManager without buffers just reports no delta. */
        free(s_traceRef); free(s_traceCur);
        s_traceRef = s_traceCur = nullptr;
        log_w("Lap trace: %u bytes of PSRAM unavailable, live delta disabled",
              (unsigned)(bytes * 2));
        return;
    }
    lapManager.setTraceBuffers(s_traceRef, s_traceCur, LAP_TRACE_POINTS);
}

/* ============================================================================
 * DEMO MODE
 * Drives the real dashboard from synthetic telemetry, so the whole live path
 * runs exactly as it does on track. Nothing is written to the card, so a demo
 * leaves no session behind. It installs its own gates — the selected track may
 * be nowhere near, or have no split gates at all — and puts the real track's
 * back on the way out.
 * ========================================================================= */
static DemoTrack s_demo;
static bool s_demoMode = false;

// Set from LVGL callbacks, consumed in loop() — same rule as charge mode.
static volatile bool s_demoEnterPending = false;
static volatile bool s_demoExitPending  = false;
static volatile bool s_demoSkipPending  = false;
/* A finish-line crossing found while draining demo frames, handed to syncUI so
 * the header and the best-lap flash still run in one place. At most one can
 * happen per pass: a pass advances about a second of simulation and a lap is
 * forty. */
static bool s_demoLapDone = false;

extern "C" void ui_helper_enter_demo_mode(void) { s_demoEnterPending = true; }
extern "C" void ui_helper_exit_demo_mode(void)  { s_demoExitPending  = true; }
extern "C" void ui_helper_demo_skip_lap(void)   { s_demoSkipPending  = true; }

/* Lap timing runs for a real session or a demo. The log enqueue deliberately
 * does not consult this — it stays gated on the session alone. */
static inline bool timingActive() {
    return logManager.isSessionActive() || s_demoMode;
}

// Push a track's split gates into LapManager. Must follow setFinishLine(),
// which clears them — the splits belong to a track, so a new finish line
// invalidates them.
static void applySectorGates(const TrackConfig *t) {
    if (!t) return;
    FinishLine s1 = { t->s1.left_lat, t->s1.left_lon, t->s1.right_lat, t->s1.right_lon };
    FinishLine s2 = { t->s2.left_lat, t->s2.left_lon, t->s2.right_lat, t->s2.right_lon };
    lapManager.setSectorGates(t->s1.usable() ? &s1 : nullptr,
                              t->s2.usable() ? &s2 : nullptr);
}

// Install a track into LapManager: finish line first, then the splits it
// clears. Every path that re-points lap timing goes through here, because the
// two calls have to happen in that order.
static void applyTrack(const TrackConfig *t) {
    if (!t || !t->left_valid || !t->right_valid) return;
    FinishLine fl = { t->left_lat, t->left_lon, t->right_lat, t->right_lon };
    lapManager.setFinishLine(fl);
    applySectorGates(t);
}

static const TrackConfig *selectedTrack() {
    return configManager.getTrack((int)configManager.getSelectedTrack());
}

static void enterDemoMode() {
    if (s_demoMode) return;
    if (logManager.isSessionActive()) {
        /* Refused rather than silently stopping the session: a demo is not
         * worth ending a recording for. */
        log_w("DEMO: a session is recording — stop it first");
        return;
    }

    s_demo.begin();

    FinishLine g[DemoTrack::GATE_COUNT];
    s_demo.gates(g);
    lapManager.setFinishLine(g[DemoTrack::GATE_END]);
    lapManager.setSectorGates(&g[DemoTrack::GATE_S1], &g[DemoTrack::GATE_S2]);

    s_lapCount = 0;
    s_demoMode = true;

    /* Under the display lock: this runs in loop(), not in LVGL context. */
    bsp_display_lock(0);
    uiHelper.setLap(0, "", "");
    uiHelper.setDemoState(true);
    bsp_display_unlock();

    log_i("DEMO: started on the synthetic oval");
}

static void exitDemoMode() {
    if (!s_demoMode) return;
    s_demoMode = false;
    s_lapCount = 0;

    /* Put the real track back. setFinishLine() also clears the reference trace,
     * so the demo's laps cannot become the delta reference for a real session. */
    applyTrack(selectedTrack());

    bsp_display_lock(0);
    uiHelper.setDemoState(false);
    uiHelper.setLap(0, "", "");
    uiHelper.setLiveDelta(0, false);
    uiHelper.setDeltaBar(0, false);
    uiHelper.setLapClock(0);
    bsp_display_unlock();

    log_i("DEMO: stopped, real track restored");
}

// ============================================================================
// CHARGE MODE
// Entered only from the CHARGE MODE button on the config screen — never
// automatically — so debugging and bench testing are unaffected.
// Parks the device so the charger's current budget goes to the cell instead of
// the load: GPS off, session stopped, CPU down, backlight on a timeout.
// ============================================================================
#define CHARGE_CPU_MHZ              80     // USB-CDC needs >= 80MHz; do not go lower
#define NORMAL_CPU_MHZ              240
#define CHARGE_BACKLIGHT_TIMEOUT_MS 15000  // both on entry and after a tap
#define CHARGE_BATT_INTERVAL_MS     5000
#define CHARGE_LOOP_DELAY_MS        50

static bool s_chargeMode         = false;
static bool s_chargeBacklightOn  = false;
static uint32_t s_chargeBacklightOnMs = 0;
static uint32_t s_chargeBattReadMs    = 0;

// Set from LVGL event callbacks, consumed in loop(). The callbacks must stay
// trivial: entering/leaving charge mode blocks for seconds, which would wedge
// the LVGL task while it holds the display lock.
static volatile bool s_chargeEnterPending = false;
static volatile bool s_chargeExitPending  = false;
static volatile bool s_chargeTouchPending = false;

extern "C" void ui_helper_enter_charge_mode(void) { s_chargeEnterPending = true; }
extern "C" void ui_helper_exit_charge_mode(void)  { s_chargeExitPending  = true; }
extern "C" void ui_helper_charge_screen_touched(void) { s_chargeTouchPending = true; }

// ============================================================================
// DISPLAY-WEDGE SAFETY NET (ALWAYS ON) - "indestructible" recovery.
// The AXS15231B QSPI panel write (panel_axs15231b_draw_bitmap) can occasionally
// hang the LVGL task while it holds the display lock, which would otherwise
// freeze the whole UI forever (screen + touch + serial dead). The spin cannot
// be safely unstuck in place, so we guarantee recovery instead: a short Task
// Watchdog on loopTask converts a wedge into a fast auto-reset (~5s) and the
// dash comes straight back.
// Timeout keeps headroom over legitimate slow paths (SD writes) so it only ever
// fires on a genuine *infinite* wedge, never on a transient stall.
// ============================================================================
#define DISPLAY_WDT_TIMEOUT_MS 5000

// Recovery counter persisted across the panic-reset (RTC memory survives a SW
// reset; it is garbage on a true power-on, so guard it with a magic value).
RTC_NOINIT_ATTR static uint32_t s_recoveryMagic;
RTC_NOINIT_ATTR static uint32_t s_recoveryCount;

static void displayWdtInit() {
    esp_task_wdt_config_t twdt = {};
    twdt.timeout_ms     = DISPLAY_WDT_TIMEOUT_MS;
    twdt.idle_core_mask = 0;
    twdt.trigger_panic  = true;   // a genuine loopTask wedge -> fast auto-reset
    if (esp_task_wdt_reconfigure(&twdt) != ESP_OK) {
        esp_task_wdt_init(&twdt);   // not yet initialized by the Arduino core
    }
    esp_task_wdt_add(NULL);          // subscribe loopTask; fed each loop()
}

static void reportRecoveryOnBoot() {
    esp_reset_reason_t rr = esp_reset_reason();
    if (s_recoveryMagic != 0xC0FFEE42u) {   // fresh power-on: RTC RAM is garbage
        s_recoveryMagic = 0xC0FFEE42u;
        s_recoveryCount = 0;
    }
    if (rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT ||
        rr == ESP_RST_WDT      || rr == ESP_RST_PANIC) {
        s_recoveryCount++;
        log_w("=== AUTO-RECOVERED from display wedge (reset_reason=%d, recoveries this power cycle=%u) ===",
              (int)rr, s_recoveryCount);
    } else {
        s_recoveryCount = 0; // normal power-on / flash / external reset
    }
}

// ============================================================================
// BRIDGES (called from LVGL event callbacks via ui_theme.cpp)
// Already in LVGL task context — do NOT call bsp_display_lock here.
// ============================================================================
extern "C" void ui_helper_apply_finish_line(double ll, double ln, double rl, double rn) {
    FinishLine fl = { ll, ln, rl, rn };
    lapManager.setFinishLine(fl);
    // setFinishLine() clears the split gates by design — a new finish line
    // belongs to a different track, so stale splits would be wrong. That means
    // every caller must put them back, or saving a track silently kills sector
    // timing until the next reboot.
    applySectorGates(configManager.getTrack((int)configManager.getSelectedTrack()));
    s_lapCount = 0; // Reset lap counter when finish line changes
}

extern "C" bool ui_helper_get_gps(double *lat, double *lon) {
    if (!telemetry.hasFix) return false;
    *lat = telemetry.lat;
    *lon = telemetry.lng;
    return true;
}

extern "C" void ui_helper_toggle_session() {
    if (logManager.isSessionActive()) {
        logManager.stopSession();
        uiHelper.setSessionState(false);
#if defined(ENABLE_GOPRO)
        goPro.setRecording(false);
#endif
    } else {
        logManager.startSession();
        uiHelper.setSessionState(true);
#if defined(ENABLE_GOPRO)
        // Non-blocking: the worker connects (waking a sleeping camera) and
        // rolls the shutter as soon as the link is up.
        goPro.setRecording(true);
#endif

        // Re-apply the active track's finish line to lapManager on session start
        const TrackConfig *active = selectedTrack();
        if (active && active->left_valid && active->right_valid) {
            applyTrack(active);
            s_lapCount = 0;
        }
    }
}

extern "C" void ui_helper_stop_session() {
    logManager.stopSession();
    uiHelper.setSessionState(false);

    /* Put the dashboard back to cold. The per-frame readouts follow
     * timingActive() and blank themselves, but the header's LAP n / lap time /
     * BEST are only written on a crossing. */
    s_lapCount = 0;
    uiHelper.setLap(0, "", "");
#if defined(ENABLE_GOPRO)
    goPro.setRecording(false);
#endif
}

void syncUI() {
    bsp_display_lock(0);
    // 1. Consume the latest locally assembled telemetry frame
    if (newTelemetryAvailable) {
        newTelemetryAvailable = false;

        uiHelper.setGx(telemetry.gForceX);
        uiHelper.setGy(telemetry.gForceY);

        /* The log enqueue used to live here. It now happens per-frame in loop(),
         * where every NAV-PVT is seen — this path only runs once per UI pass and
         * so could never carry more than one frame of the 25. */

        uiHelper.setSpeed(telemetry.speedKmph);

        // 8. Update GPS Satellite Indicator
        uint8_t sats = telemetry.sats;
        static uint8_t lastSats = 255; // Use 255 so it guarantees an update on the very first loop
        if (sats != lastSats) {
            lastSats = sats;
            
            uiHelper.setGps(lastSats);
        }

        // 9. Lap Detection — only while a session is active. Demo frames are
        // already fed to LapManager as they are drained, so they arrive here as
        // a flag rather than being processed twice.
        bool lapDone = s_demoMode ? s_demoLapDone
                                  : (timingActive() && lapManager.processTelemetry(telemetry));
        s_demoLapDone = false;
        if (lapDone) {
            s_lapCount++;
            uint64_t lt = lapManager.getLastLapTime();
            uint64_t bt = lapManager.getBestLapTime();
            // LapManager's own verdict rather than a second definition: the
            // same flag decides whether this lap's trace becomes the delta
            // reference, so purple and the reference cannot disagree.
            bool isBest = lapManager.wasBestLap();

            char lapStr[20], bestStr[20];
            dashFmtTime(lapStr, sizeof(lapStr), (uint32_t)lt);
            if (bt != 0xFFFFFFFFFFFFFFFFULL)
                dashFmtTime(bestStr, sizeof(bestStr), (uint32_t)bt);
            else
                bestStr[0] = '\0';

            // The number and the time have to describe the SAME lap.
            // s_lapCount counts crossings, so after the Nth crossing you are
            // driving lap N while the time just measured belongs to lap N-1.
            // Nothing to report on the first crossing: it only starts the
            // clock.
            uint8_t lapJustDone = (s_lapCount > 0) ? (uint8_t)(s_lapCount - 1) : 0;
            if (lt) uiHelper.setLap(lapJustDone, lapStr, bestStr);

            // Purple at the line, held for a few seconds before the new lap's
            // live delta takes the panel back. Green and red need nothing
            // here: they already come from the live delta.
            //
            // Measured against the best lap as it stood BEFORE this one, which
            // is the time you were chasing — against the current best it would
            // read 0.00 on every personal best.
            if (isBest) {
                uint64_t pb = lapManager.getPreviousBestLapTime();
                bool comparable = (pb != 0 && pb != 0xFFFFFFFFFFFFFFFFULL);
                uiHelper.flashBestLap(
                    comparable ? (float)((int64_t)lt - (int64_t)pb) / 1000.0f : 0.0f,
                    comparable);
            }

            if (lt)
                log_i("Lap %u completed: %s%s (best: %s)", (unsigned)lapJustDone,
                      lapStr, isBest ? " [BEST]" : "", bestStr);
            else
                log_i("Finish line crossed — lap timing starts here");
        }
    }

    // One reading for the three blocks below, which all have to agree: a
    // session stopping between them would show a blanked band beside a live
    // delta.
    const bool timing = timingActive();

    // Driven every frame from LapManager rather than on crossing events, so
    // the display cannot drift out of step with the timer.
    {
        int64_t  sd[3];
        uint32_t stime[3];
        bool     sv[3];
        for (int i = 0; i < 3; i++) {
            sd[i]    = timing ? lapManager.getSectorDelta(i) : LapManager::LAP_SECTOR_NO_DELTA;
            stime[i] = timing ? (uint32_t)lapManager.getSectorTime(i) : 0;
            sv[i]    = timing && lapManager.isSectorValid(i);
        }
        /* -1 unless a lap is genuinely under way: on the out lap the kart
         * drives through the split gates, so getCurrentSector() reports 1 then
         * 2 for a lap that has not started. */
        int cur = (timing && lapManager.hasSectors() && lapManager.isLapUnderWay())
                    ? lapManager.getCurrentSector() : -1;
        uiHelper.setSectors(cur,
                            (uint32_t)lapManager.getRunningSplitMs(telemetry.timestamp),
                            sd, stime, sv);
    }

    // Read every frame, like the band above; the helper diffs and rate-limits.
    {
        /* Gated on the timing state, not just LapManager's own: once a session
         * stops, processTelemetry() is not called, so the delta would keep its
         * last value and the panel would show the gap from a finished run. */
        int64_t d = timing ? lapManager.getLiveDeltaMs() : LapManager::LAP_NO_DELTA;
        bool    ok = (d != LapManager::LAP_NO_DELTA);
        uiHelper.setLiveDelta(ok ? (float)d / 1000.0f : 0.0f, ok);

        // Where the lap is heading, and the bar — which takes the SPLIT delta,
        // not this one.
        uiHelper.setPredicted(lapManager.getPredictedLapMs(), ok);

        int64_t sd = timing ? lapManager.getSplitDeltaMs() : LapManager::LAP_NO_DELTA;
        bool    sok = (sd != LapManager::LAP_NO_DELTA);
        uiHelper.setDeltaBar(sok ? (float)sd / 1000.0f : 0.0f, sok);
    }

    // Off the same GPS timestamps the lap times come from, so it lands exactly
    // on the lap time as the line comes round. Zeroed with no session:
    // currentLapStartTime outlives a stopped one.
    {
        uint32_t lapMs = timing
                           ? (uint32_t)lapManager.getRunningLapMs(telemetry.timestamp)
                           : 0;
        uiHelper.setLapClock(lapMs);
    }

    // Drive the recording panel blink
    uiHelper.tickRecordingPanel();

    bsp_display_unlock();
}

void setup() {
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT == 1
    // FIRST, before anything can write a byte, and before setDebugOutput routes
    // the IDF log here. With no host draining the port — the normal case in the
    // kart, and equally a USB cable with no monitor open — the CDC buffer fills
    // and log_*() BLOCKS the calling task, leaving the dash dark until a serial
    // monitor connects.
    //
    // This cannot protect the Arduino core's own boot banner: printBeforeSetupInfo()
    // runs before setup() is ever called, so its few hundred lines block with the
    // default timeout. That one is silenced by keeping CORE_DEBUG_LEVEL below
    // DEBUG — see the note in platformio.ini.
    Serial.setTxTimeoutMs(0);
    delay(2000);
#endif
    Serial.setDebugOutput(true);

    // FIRST: everything logged from here on is captured for the log screen and,
    // once the card mounts, written to /dash.log. Installed before the boot
    // banner so a GPS or SD failure at the track has an account on the device
    // rather than only on a USB port nothing is attached to.
    logBuffer.begin();

    log_i("--- KART DISPLAY BOOTING ---");

    // Report whether this boot is a normal start or an auto-recovery from a
    // display wedge (Option 4 safety net).
    reportRecoveryOnBoot();

    // Before anything can apply a finish line: setFinishLine() clears the
    // traces, and handing the buffers over afterwards would be harmless but
    // this keeps the ordering obvious.
    initLapTrace();

    // 1. Initialize IMU FIRST
#if defined(ENABLE_IMU)
    log_i("Initializing IMU on SDA:%d SCL:%d...", I2C_SDA, I2C_SCL);

    // We pass the pins directly to imu.begin to handle bus recovery and Wire.begin internally
    if (imu.begin(I2C_SDA, I2C_SCL, 100000)) {
        log_i("IMU: OK");
        imuReady = true;
    } else {
        log_e("IMU: Error - Check wiring on expansion port (GPIO 9/10)");
    }
#else
    log_i("Display IMU disabled (ENABLE_IMU not defined)");
#endif

    // 2. Initialize UI
    log_i("Display BSP init...");
    uiHelper.init();

    // 3. Initialize Managers
    logManager.begin();
    displayBattery.begin();

    if (!gps.begin()) {
        log_e("GPS failed to start on RX=%d TX=%d!", GPS_STANDALONE_RX, GPS_STANDALONE_TX);
    }

#if defined(ENABLE_GOPRO)
    // BLE camera link. Pass a name fragment (e.g. "1234" from "GoPro 1234") to
    // pin the dash to one camera when several are in range.
    goPro.begin(
#if defined(GOPRO_NAME_FILTER)
        GOPRO_NAME_FILTER
#endif
    );
#endif

    // 4. Load config from SD and apply saved theme
    configManager.begin();
    bsp_display_lock(0);
    uiHelper.setTheme(configManager.getTheme() == 0 ? DASH_MODE_NIGHT : DASH_MODE_DAY);
    bsp_display_unlock();

    // 4. Load config + tracks from SD and apply to UI
    configManager.begin();
    bsp_display_lock(0);

    uiHelper.setTheme(configManager.getTheme() == 0 ? DASH_MODE_NIGHT : DASH_MODE_DAY);

    // Build name pointer array directly into ConfigManager's stable storage
    static const char *track_name_ptrs[CONFIG_MAX_TRACKS];
    int track_count = configManager.getTrackCount();
    for (int i = 0; i < track_count; i++) {
        const TrackConfig *t = configManager.getTrack(i);
        track_name_ptrs[i] = t ? t->name : "";
    }
    uiHelper.setTracks(track_name_ptrs, track_count);

    int sel = (int)configManager.getSelectedTrack();
    uiHelper.setTrackIdx(sel);

    const TrackConfig *active = configManager.getTrack(sel);
    if (active) {
        uiHelper.setStartL(active->left_lat, active->left_lon, active->left_valid);
        uiHelper.setStartR(active->right_lat, active->right_lon, active->right_valid);
        // The split-gate rows need populating here too. Stepping tracks calls
        // apply_track_coords() which does all six, but nothing does at boot —
        // so a tracks.ini with sectors still showed "-- not set --" until you
        // touched the stepper.
        uiHelper.setSectorCoord(0, SETUP_LINE_L, active->s1.left_lat,  active->s1.left_lon,  active->s1.left_valid);
        uiHelper.setSectorCoord(0, SETUP_LINE_R, active->s1.right_lat, active->s1.right_lon, active->s1.right_valid);
        uiHelper.setSectorCoord(1, SETUP_LINE_L, active->s2.left_lat,  active->s2.left_lon,  active->s2.left_valid);
        uiHelper.setSectorCoord(1, SETUP_LINE_R, active->s2.right_lat, active->s2.right_lon, active->s2.right_valid);
        applyTrack(active);
    }

    bsp_display_unlock();

#if defined(ENABLE_IMU)
    if (imuReady) {
        calibManager.begin();
    }
#endif

    // Display-wedge safety net (always on): a wedge -> ~5s auto-reset, not a freeze.
    displayWdtInit();
    log_i("Display watchdog armed: loopTask WDT=%dms (panic+reset on wedge)", DISPLAY_WDT_TIMEOUT_MS);

    log_i("Display System Ready.");
}

static void enterChargeMode() {
    // gps.standby() rather than gps.end(): end() only closes the ESP32's UART
    // and leaves the module tracking three constellations at 10 Hz, which is
    // why this mode never saved much. The UART stays open so wake() can talk to
    // it again.
    //
    // Be clear about the ceiling: this receiver has no software power-down at
    // all, so its RF front end keeps drawing whatever it draws. Charge mode's
    // real savings are the backlight and the CPU clock.
    log_i("Charge mode: parking device (GPS quiesced, CPU %dMHz).",
          CHARGE_CPU_MHZ);

    logManager.stopSession();
    gps.standby();

    bsp_display_lock(0);
    uiHelper.setSessionState(false);
    uiHelper.setChargeBattery(255, 0.0f);
    uiHelper.showChargeScreen();
    bsp_display_unlock();

    // Drop the clock only after the screen swap has been handed to LVGL, so the
    // transition still renders at full speed.
    setCpuFrequencyMhz(CHARGE_CPU_MHZ);

    bsp_display_backlight_on();
    s_chargeBacklightOn   = true;
    s_chargeBacklightOnMs = millis();
    s_chargeBattReadMs    = 0;     // force a reading on the first service pass
    s_chargeMode          = true;
}

static void exitChargeMode() {
    setCpuFrequencyMhz(NORMAL_CPU_MHZ);
    bsp_display_backlight_on();
    s_chargeBacklightOn = true;

    // wake() restores the constellation, message set and fix rate, then lets
    // configureAtgm336() verify. It takes ~2.5s, which is close enough to the 5s
    // loopTask watchdog to be worth stepping out of it.
    //
    // No gps.begin() here: the UART was never closed and the module was never
    // asleep, so there is nothing to re-probe. Probing was in fact harmful — it
    // ran while the port was already open ("RX Buffer can't be resized") and,
    // if the sentences had not come back, reported the module as missing.
    esp_task_wdt_delete(NULL);
    gps.wake();
    esp_task_wdt_add(NULL);

    bsp_display_lock(0);
    uiHelper.hideChargeScreen();
    bsp_display_unlock();

    s_chargeMode = false;
    log_i("Charge mode: resumed at %dMHz.", NORMAL_CPU_MHZ);
}

static void serviceChargeMode(uint32_t now) {
    // A tap anywhere wakes the backlight and restarts the timeout.
    if (s_chargeTouchPending) {
        s_chargeTouchPending = false;
        if (!s_chargeBacklightOn) {
            bsp_display_backlight_on();
            s_chargeBacklightOn = true;
            // On-screen value was sampled dark, so it reads ~30mV high until the
            // panel loads the cell. Resample now instead of letting it visibly drop.
            s_chargeBattReadMs = 0;
        }
        s_chargeBacklightOnMs = now;
    }

    if (s_chargeBacklightOn && (now - s_chargeBacklightOnMs >= CHARGE_BACKLIGHT_TIMEOUT_MS)) {
        bsp_display_backlight_off();
        s_chargeBacklightOn = false;
    }

    if (now - s_chargeBattReadMs >= CHARGE_BATT_INTERVAL_MS) {
        s_chargeBattReadMs = now;
        float volts  = displayBattery.getVoltage();
        uint8_t pct  = (uint8_t)displayBattery.getPercentage();

        bsp_display_lock(0);
        uiHelper.setChargeBattery(pct, volts);
        bsp_display_unlock();

        log_i("Charge mode: %d%% (%.2fV)", pct, volts);
    }
}

void loop() {
    uint32_t now = millis();

    esp_task_wdt_reset();   // feed the display-wedge watchdog (always on)

    // Transitions run here, never in LVGL callback context.
    if (s_chargeEnterPending) {
        s_chargeEnterPending = false;
        if (!s_chargeMode) enterChargeMode();
    }
    if (s_chargeExitPending) {
        s_chargeExitPending = false;
        if (s_chargeMode) exitChargeMode();
    }
    if (s_demoEnterPending) { s_demoEnterPending = false; enterDemoMode(); }
    if (s_demoExitPending)  { s_demoExitPending  = false; exitDemoMode();  }
    if (s_demoSkipPending) {
        s_demoSkipPending = false;
        /* Ignored outside a demo: the lap clock is a live readout there, not a
         * control, and there is no synthetic lap to skip. */
        if (s_demoMode) {
            s_demo.skipToNextLap();
            log_i("DEMO: skipping to the next lap");
        }
    }

    // Ahead of everything below on purpose: charge mode is a parking state, so
    // it must not fall through to session analysis (a multi-second SD stream)
    // or the dashboard repaint. Both would defeat the point of the mode.
    if (s_chargeMode) {
        serviceChargeMode(now);
        delay(CHARGE_LOOP_DELAY_MS);
        return;
    }

    // Session analysis runs here, deliberately before the display lock is
    // taken: streaming a log takes many seconds, and doing it while holding the
    // lock would block this task and trip the watchdog.
    sessionBrowser.service();

    // Flush captured log lines to SD. Here for the same reason as the line
    // above: it touches the card, and must not run under the display lock.
    logBuffer.service();

#if defined(ENABLE_IMU)
    if (imuReady) {
        float currentSteering = calibManager.getSteeringAngle();

        if (calibManager.isDone() && (now - lastImuReadMs >= timeBetweenImuReads)) {
            latestImuData = imu.update(currentSteering);
            lastImuReadMs = now;
        }
    }
#endif

    /* Drain EVERY NAV-PVT frame, not one per pass. update() yields a single
     * frame per call now, so this loop logs all 25 per second even when a slow
     * LVGL or SD pass makes loop() itself run at 19Hz — which is exactly how a
     * measured session lost 24% of its rows without the health counter noticing,
     * the frames being discarded upstream of the queue. The UI still consumes
     * only the newest via newTelemetryAvailable; only the log needs all of them. */
    /* Demo mode substitutes for the receiver entirely. Same while-loop shape
     * as the real drain below, so a slow pass catches up.
     *
     * Unlike the real drain, every frame goes into LapManager here rather than
     * only the newest surviving to syncUI. A skip emits up to 25 frames in one
     * pass, and handing over just the last of them leaves consecutive fixes a
     * second of simulation apart - about 22 m - which steps straight over a
     * 14 m gate and loses the lap. */
    while (s_demoMode && s_demo.update(now, telemetry)) {
        newTelemetryAvailable = true;
        if (timingActive() && lapManager.processTelemetry(telemetry))
            s_demoLapDone = true;
    }

    while (!s_demoMode && gps.update()) {
        /* IMU removed — no accelerometer on this build. The g-force and gyro
         * fields stay in the message (and the CSV) as zeros so old sessions and
         * SessionBrowser keep parsing; a future 9-DoF refills them. */
        telemetry.type        = MSG_TELEMETRY;
        telemetry.speedKmph   = (float)gps.getSpeed(0.0f, 0.0f);
        telemetry.gForceX     = 0.0f;
        telemetry.gForceY     = 0.0f;
        telemetry.totalGForce = 0.0f;
        telemetry.gyroZ       = 0.0f;
        telemetry.lat         = gps.getLat();
        telemetry.lng         = gps.getLng();
        telemetry.sats        = (uint8_t)gps.getSatellites();
        telemetry.hasFix      = gps.hasFix() ? 1 : 0;
        telemetry.timestamp   = gps.getEpochMs();

        // Fix quality, zeroed on providers that cannot report it (the ATGM336).
        GpsFixInfo fix        = gps.getFixInfo();
        telemetry.fixType     = fix.fixType;
        telemetry.pdop        = fix.pdop;
        telemetry.hAccM       = fix.hAccM;
        telemetry.sAccMps     = fix.sAccMps;

        /* Enqueue HERE, once per frame. This used to live in syncUI() behind the
         * newTelemetryAvailable flag, which is a one-slot handoff: whatever
         * arrived between UI passes was overwritten and never reached the queue.
         * Zero timeout still, so a full queue drops the row rather than blocking
         * the UI — but now that is a real drop the health counter can see. */
        if (LogManager::logQueue != NULL && logManager.isSessionActive()) {
            if (xQueueSend(LogManager::logQueue, &telemetry, 0) != pdTRUE) {
                logManager.noteDroppedFrame();
            }
        }

        newTelemetryAvailable = true;
    }

    // Display battery ADC read — done before the display lock to avoid blocking LVGL
    static uint8_t cachedDisplayBattPct = 255;
    static uint8_t lastDisplayBattPct = 255;
    if (now - lastDisplayBattReadMs >= DISPLAY_BATT_READ_INTERVAL_MS) {
        lastDisplayBattReadMs = now;
        cachedDisplayBattPct = (uint8_t)displayBattery.getPercentage();
        log_i("Display battery: %d%%", cachedDisplayBattPct);
    }

    /* GPS heartbeat. "GPS 0" on the dash says a fix is missing but not why, and
     * the difference matters: satellites climbing with fixType stuck at 0 is a
     * sky-view/cold-start problem and will come good if left alone; zero
     * satellites for minutes is an antenna, wiring or config problem and never
     * will. Logged unconditionally so a session that never got a fix leaves an
     * account of itself on the card. */
    static uint32_t lastGpsLogMs   = 0;
    static uint32_t lastGpsFrames  = 0;
    if (now - lastGpsLogMs >= 5000) {
        /* Measured NAV-PVT rate, not the rate we asked for. CFG-RATE is accepted
         * without complaint even when the receiver cannot sustain it, so the only
         * way to know we are really getting 25Hz is to count frames and divide. */
        uint32_t frames  = gps.getFrameCount();
        uint32_t elapsed = now - lastGpsLogMs;
        float    hz      = lastGpsLogMs && elapsed
                             ? (frames - lastGpsFrames) * 1000.0f / (float)elapsed
                             : 0.0f;
        lastGpsLogMs  = now;
        lastGpsFrames = frames;

        /* gps.update() is never called in demo mode, so the frame counter does
         * not move. Say so rather than reporting 0.0Hz, which reads as a dead
         * receiver. */
        if (s_demoMode) {
            log_i("GPS: DEMO MODE — synthetic telemetry, receiver not being read");
        } else {
            GpsFixInfo fi = gps.getFixInfo();
            log_i("GPS: sats=%lu fix=%u ok=%d pdop=%.1f hAcc=%.1fm rate=%.1fHz",
                  (unsigned long)gps.getSatellites(), (unsigned)fi.fixType,
                  fi.gnssFixOK ? 1 : 0, fi.pdop, fi.hAccM, hz);
        }
    }

    // Once-a-second health reporting
    if (now - lastHealthReportMs >= 1000) {
        lastHealthReportMs = now;

        // QSPI flush health: report dropped frames and the worst bus-idle wait in
        // the last second, then reset the stall high-water mark. A blink should line
        // up with either a frame_drops increment (hard skip) or a high max_stall.

        /* Enqueue HERE, once per frame. This used to live in syncUI() behind the
         * newTelemetryAvailable flag, which is a one-slot handoff: whatever
         * arrived between UI passes was overwritten and never reached the queue.
         * Zero timeout still, so a full queue drops the row rather than blocking
         * the UI — but now that is a real drop the health counter can see. */
        if (LogManager::logQueue != NULL && logManager.isSessionActive()) {
            if (xQueueSend(LogManager::logQueue, &telemetry, 0) != pdTRUE) {
                logManager.noteDroppedFrame();
            }
        }

        static uint32_t lastFrameDrops = 0;
        uint32_t drops = lvgl_port_frame_drops;
        uint32_t stallUs = lvgl_port_max_stall_us;
        lvgl_port_max_stall_us = 0;
        if (drops != lastFrameDrops || stallUs >= 8000) {
            log_w("[DISP] frame_drops=%lu (+%lu)  max_bus_stall=%lu us",
                  (unsigned long)drops, (unsigned long)(drops - lastFrameDrops),
                  (unsigned long)stallUs);
            lastFrameDrops = drops;
        }

        // Log rows the queue had no room for. Also persisted to /health.csv by the
        // log task, since serial is no use with the dash bolted to a steering wheel.
        static uint32_t lastLogQueueDrops = 0;
        uint32_t logDrops = logManager.droppedFrames();
        if (logDrops != lastLogQueueDrops) {
            log_w("[LOG] queue_drops=%lu (+%lu) — the card is not keeping up",
                  (unsigned long)logDrops,
                  (unsigned long)(logDrops - lastLogQueueDrops));
            lastLogQueueDrops = logDrops;
        }

#if defined(ENABLE_IMU)
        if (imuReady) {
            float steeringDeg = calibManager.getSteeringAngle();
            latestImuData = imu.update(steeringDeg);
            log_i("Steering: %.1f deg | G: %.2f", steeringDeg, latestImuData.gForce);
        }
#endif
    }

    bsp_display_lock(0);

#if defined(ENABLE_IMU)
    if (imuReady) {
        calibManager.update();
    }
#endif

    if (cachedDisplayBattPct != lastDisplayBattPct) {
        lastDisplayBattPct = cachedDisplayBattPct;
        uiHelper.setDisplay(cachedDisplayBattPct);
    }

    // Alert banner. setAlert() early-outs when the counts are unchanged-and-zero,
    // so this is cheap enough to run every pass.
    uiHelper.setAlert(logBuffer.errorCount(), logBuffer.warningCount());

#if defined(ENABLE_WEB_PORTAL)
    // Client count is the only thing that changes while the portal is up, and
    // it only matters at human speed.
    if (now - lastWebUiMs >= WEB_PORTAL_UI_INTERVAL_MS) {
        lastWebUiMs = now;
        bool up = webPortal.isRunning();
        String wip = up ? webPortal.ip() : String();
        uiHelper.setWifi(up, webPortal.clients(), up ? wip.c_str() : nullptr);
    }
#endif

#if defined(ENABLE_GOPRO)
    // Camera state is pushed by the GoPro over BLE; mirror it onto the status
    // bar at a human rate rather than every frame. setCamera() self-filters, so
    // an unchanged snapshot costs nothing.
    if (now - lastGoProUiMs >= GOPRO_UI_INTERVAL_MS) {
        lastGoProUiMs = now;
        GoProStatus cam = goPro.status();
        uiHelper.setCamera(cam.linked, cam.recording, cam.batteryPct, cam.gpsLock);
    }
#endif

    // Update UI Elements
    syncUI();

    bsp_display_unlock();

    delay(1);
}