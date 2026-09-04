#ifndef DEMO_TRACK_H
#define DEMO_TRACK_H

/* ============================================================================
 * DemoTrack — synthetic telemetry for the dashboard's DEMO mode.
 *
 * Drives an imaginary kart round an oval, emitting TelemetryMsg frames at the
 * same 25 Hz the real receiver does, so demo mode runs the whole live pipeline
 * rather than posing a static screen. It carries its own finish line and split
 * gates, because the track selected on the device may be miles away or have no
 * split gates at all.
 *
 * Free of Arduino and LVGL so tools/lapreplay/demo can verify the geometry on
 * the host — a gate with its posts swapped is not a crash, it is a lap counter
 * that silently never increments.
 * ========================================================================= */

#include <math.h>
#include <stdint.h>
#include "EspNowProtocol.h"
/* For FinishLine, which gates() fills. */
#include "LapManager.h"

class DemoTrack {
public:
    /* Gate indices match LapManager's LAP_GATE_*. */
    enum { GATE_S1 = 0, GATE_S2 = 1, GATE_END = 2, GATE_COUNT = 3 };

    /* Frame interval, matching the u-blox rate the dash is configured for. */
    static const uint32_t STEP_MS = 40;

    /* Most simulated time any one update() pass may advance. At STEP_MS that is
     * 25 frames, which is a pass's worth of work rather than a whole lap's. */
    static const uint32_t MAX_CATCHUP_MS = 1000;

    /* How much faster than real time a skip runs. With MAX_CATCHUP_MS above,
     * a UI pass advances about a second of simulation, so a 40 s lap skips in
     * a little over a second. */
    static const uint32_t SKIP_RATE = 30;

    /* Synthetic wall-clock origin; only deltas matter. Defaulted so the
     * firmware and the replay harness cannot pick different ones. */
    static const uint64_t EPOCH_BASE_MS = 1785000000000ULL;

    void begin(uint64_t epochBaseMs = EPOCH_BASE_MS) {
        _epochBase = epochBaseMs;
        _theta     = 0.0;
        _simMs     = 0;
        _lap       = 0;
        _skipToLap = 0;
        _paceLap   = 0xFFFFFFFFu;
        _pendingMs = 0;
        _speedKmh  = 0.0;
        _rng       = 0x2545F491u;
        _started   = false;
    }

    /* Advance to `nowMs` (a millis() reading) and emit one frame per STEP_MS
     * elapsed. Call in a while loop, as the real GPS drain does. */
    bool update(uint32_t nowMs, TelemetryMsg &out) {
        if (!_started) { _lastWallMs = nowMs; _started = true; }

        uint32_t elapsed = nowMs - _lastWallMs;   /* unsigned: wrap-safe */
        if (_lap < _skipToLap) elapsed *= SKIP_RATE;
        /* After a stall (an SD flush, a screen change), and on every pass of a
         * skip, run forward at most a second: replaying a longer gap in one
         * pass teleports the kart through a gate, and a jumped gate is a
         * missed lap. It also bounds the work done before the UI redraws. */
        if (elapsed > MAX_CATCHUP_MS) elapsed = MAX_CATCHUP_MS;
        _lastWallMs = nowMs;
        _pendingMs += elapsed;

        if (_pendingMs < STEP_MS) return false;
        _pendingMs -= STEP_MS;

        step(STEP_MS);
        fill(out);
        return true;
    }

    /* Fast-forward to the next finish-line crossing, for the tap on the dash's
     * lap clock. Simulated time is what speeds up, not the kart's position:
     * it drives the rest of the lap frame by frame at SKIP_RATE, so it passes
     * the gate the normal way and the lap keeps a real time.
     *
     * Jumping the position instead would break both halves of that. A gate is
     * only detected when two consecutive fixes straddle it, and LapManager
     * rejects a lap under ten seconds, so a kart placed just short of the line
     * would either miss the gate or have its lap thrown away. */
    void skipToNextLap() { _skipToLap = _lap + 1; }

    /* True while a skip is still running. */
    bool skipping() const { return _lap < _skipToLap; }

    /* Ordered left then right as LapManager wants them: it rejects a crossing
     * whose travel direction is not 90 degrees counter-clockwise from the
     * left-to-right post vector, which the rotation below arranges. */
    void gate(int idx, double &leftLat, double &leftLng,
                       double &rightLat, double &rightLng) const {
        double ang = angleOfGate(idx) + THETA_OFFSET;
        double e = A * cos(ang), n = B * sin(ang);

        /* Tangent, i.e. the direction of travel at the gate. */
        double te = -A * sin(ang), tn = B * cos(ang);
        double mag = sqrt(te * te + tn * tn);
        te /= mag; tn /= mag;

        /* Rotate the tangent to get left-to-right across the track. */
        double ue = tn, un = -te;

        toLatLng(e - HALF_GATE_M * ue, n - HALF_GATE_M * un, leftLat,  leftLng);
        toLatLng(e + HALF_GATE_M * ue, n + HALF_GATE_M * un, rightLat, rightLng);
    }

    /* Every gate, laid out as LapManager takes them. Which post is "left" and
     * which index is the finish line is exactly what can be got wrong, so the
     * firmware and the demo harness share this one wiring. */
    void gates(FinishLine *out) const {
        for (int i = 0; i < GATE_COUNT; i++)
            gate(i, out[i].leftLat, out[i].leftLng, out[i].rightLat, out[i].rightLng);
    }

private:
    /* Oval, metres from the centre. 630 m round, a lap in the high thirties at
     * the pace below. */
    static constexpr double A = 130.0;         /* semi-axis, east  */
    static constexpr double B = 65.0;          /* semi-axis, north */

    /* Puts theta 0 on the finish line rather than at a hairpin, so a lap of the
     * parameter is a lap of the track and the pace model can key off it. */
    static constexpr double THETA_OFFSET = M_PI / 2.0;
    static constexpr double TWO_PI_D     = 6.283185307179586;
    static constexpr double HALF_GATE_M  = 7.0;

    /* Near Kartodromo Granja Viana, so the coordinates look like somewhere. */
    static constexpr double CLAT = -23.60500;
    static constexpr double CLNG = -46.83650;
    static constexpr double K_LAT = 110540.0;
    static constexpr double K_LNG = 102000.0;  /* 111320 * cos(23.6 deg) */

    static constexpr double V_MIN_KMH = 32.0;  /* hairpins   */
    static constexpr double V_MAX_KMH = 78.0;  /* straights  */

    /* Warm-up: first lap at 86% pace, closing on 100% with a 4-lap constant. */
    static constexpr double W0    = 0.86;
    static constexpr double W_TAU = 4.0;

    static double angleOfGate(int idx) {
        switch (idx) {
            case GATE_S1: return TWO_PI_D / 3.0;
            case GATE_S2: return TWO_PI_D * 2.0 / 3.0;
            default:      return 0.0;          /* END, the finish line */
        }
    }

    static void toLatLng(double e, double n, double &lat, double &lng) {
        lat = CLAT + n / K_LAT;
        lng = CLNG + e / K_LNG;
    }

    /* Cheap deterministic noise, +/- roughly half a metre: a perfectly smooth
     * path would advertise a steadiness the GPS cannot deliver. */
    double jitter() {
        _rng = _rng * 1664525u + 1013904223u;
        return ((double)((_rng >> 8) & 0xFFFF) / 65535.0 - 0.5);
    }

    double speedKmh() {
        double ang = _theta + THETA_OFFSET;
        /* Slow through the hairpins at the ends of the long axis, quick along
         * the straights. */
        double base = V_MIN_KMH + (V_MAX_KMH - V_MIN_KMH) * fabs(sin(ang));
        /* A driver warming up. The shape is chosen: a flat-plus-noise pace
         * makes the first timed lap the fastest by luck and every later one
         * slower, so the panel goes red and stays red and the demo never shows
         * green or purple. Rising-with-variation gives new bests at laps 1, 2,
         * 5, 6, 10, 11 of twelve. */
        /* Cached: these depend only on _lap, which changes every ~40 s, while
         * this runs every 40 ms. */
        if (_lap != _paceLap) {
            double warm = W0 + (1.0 - W0) * (1.0 - exp(-(double)_lap / W_TAU));
            _pace    = warm * (1.0 + 0.025 * sin(_lap * 1.30)
                                   + 0.015 * sin(_lap * 0.55 + 0.4));
            _paceLap = _lap;
        }
        const double pace = _pace;
        /* A shape that moves round the lap as well as between laps: without it
         * every lap differs from the reference by a constant and the live delta
         * is a flat line. */
        double shape = 1.0 + 0.050 * sin(_theta * 2.0 + _lap * 2.4);
        return base * pace * shape;
    }

    void step(uint32_t dtMs) {
        _speedKmh = speedKmh();
        double v  = _speedKmh / 3.6;                       /* m/s */

        double ang = _theta + THETA_OFFSET;
        double de = -A * sin(ang), dn = B * cos(ang);      /* metres per radian */
        double mag = sqrt(de * de + dn * dn);

        _theta += (v * (dtMs / 1000.0)) / mag;
        if (_theta >= TWO_PI_D) { _theta -= TWO_PI_D; _lap++; }

        _simMs += dtMs;
    }

    void fill(TelemetryMsg &out) {
        double ang = _theta + THETA_OFFSET;
        double e = A * cos(ang) + jitter();
        double n = B * sin(ang) + jitter();

        out.type      = MSG_TELEMETRY;
        out.timestamp = _epochBase + _simMs;
        out.speedKmph = (float)_speedKmh;
        /* Into aligned locals first: TelemetryMsg is packed, and a reference to
         * a packed field is a hard error on GCC. tools/lapreplay/replay.cpp
         * notes the same trap for sscanf's out-pointers. */
        double lat, lng;
        toLatLng(e, n, lat, lng);
        out.lat = lat;
        out.lng = lng;
        out.sats      = 12;
        out.hasFix    = 1;
        out.fixType   = 3;
        out.pdop      = 1.1f;
        out.hAccM     = 0.8f;
        out.sAccMps   = 0.1f;
        /* No IMU on this build; the real path sends zeros here too. */
        out.gForceX = out.gForceY = out.totalGForce = out.gyroZ = 0.0f;
        out.steeringAngle = 0.0f;
    }

    uint64_t _epochBase = 0;
    uint64_t _simMs     = 0;
    double   _theta     = 0.0;
    double   _speedKmh  = 0.0;
    uint32_t _lap       = 0;
    /* Lap number a skip is running towards; 0 when none is. */
    uint32_t _skipToLap = 0;
    /* Per-lap pace, cached against the lap it was computed for. */
    double   _pace      = 1.0;
    uint32_t _paceLap   = 0xFFFFFFFFu;
    uint32_t _lastWallMs = 0;
    uint32_t _pendingMs = 0;
    uint32_t _rng       = 0x2545F491u;
    bool     _started   = false;
};

#endif /* DEMO_TRACK_H */
