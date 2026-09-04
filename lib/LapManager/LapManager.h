#ifndef LAP_MANAGER_H
#define LAP_MANAGER_H

#include <Arduino.h>
#include "EspNowProtocol.h" // For TelemetryMsg/Data

// Define the track width by placing a point on the left and right sides
struct FinishLine {
    double leftLat;
    double leftLng;
    double rightLat;
    double rightLng;
};

/* One sample of a lap's path. Scaled ints rather than doubles because two
 * laps' worth live for the whole session; 1e-7 degrees is about 1.1 cm, two
 * orders below the fix noise this is matched against. */
struct LapTracePoint {
    int32_t  lat;    /* degrees * 1e7 */
    int32_t  lng;
    uint32_t tMs;    /* ms since this lap's interpolated start-line crossing */
};

/* Gate / sector indices. A sector is closed by the gate of the same index and
 * opened by the previous one, so:
 *
 *   sector 0  END -> S1     sector 1  S1 -> S2     sector 2  S2 -> END
 *
 * The finish line doubles as the END gate. */
enum {
    LAP_GATE_S1  = 0,
    LAP_GATE_S2  = 1,
    LAP_GATE_END = 2,
    LAP_GATE_COUNT = 3,
};

class LapManager {
public:
    void setFinishLine(const FinishLine& line);

    /* Optional split gates. Pass nullptr for either to leave it unset — with no
     * split gates the class behaves exactly as before and only times whole
     * laps. Call after setFinishLine(), which clears them. */
    void setSectorGates(const FinishLine *s1, const FinishLine *s2);

    /* Per-sample gate tracing. Useful when debugging a finish line live, but it
     * emits two or three formatted lines per sample near the gate — replaying a
     * 9,500-row session offline produces thousands of them over USB-CDC, which
     * is slow enough to matter. Off for offline analysis. */
    void setVerbose(bool v) { _verbose = v; }

    // Returns true if a lap was just completed
    bool processTelemetry(const TelemetryMsg& data);

    uint64_t getLastLapTime() const { return lastLapTimeMs; }
    uint64_t getPreviousLapTime() const { return previousLapTimeMs; }
    uint64_t getBestLapTime() const { return bestLapTimeMs; }

    /* Best lap as it stood *before* the lap just completed. This is what the
     * delta should be measured against: comparing against getBestLapTime()
     * would read 0.00 on every new personal best, because the best has already
     * been updated to the lap you are comparing. UINT64_MAX until a second lap
     * exists, i.e. until there is something to compare with. */
    uint64_t getPreviousBestLapTime() const { return previousBestLapTimeMs; }

    /* ---- live delta ----------------------------------------------------
     * The lap in progress is recorded as position plus elapsed time, the best
     * lap so far is kept as a reference trace, and each fix is matched to the
     * nearest point on it. So the delta answers "against my best lap, where am
     * I now" rather than only "how did that lap end". */

    static const int64_t LAP_NO_DELTA = INT64_MIN;

    /* Two buffers of `capacity` points: the reference lap and the lap in
     * progress. Nothing is recorded until this is called.
     *
     * The storage is the caller's because the firmware wants it in PSRAM, and
     * tools/lapreplay compiles this class for the host where there is no PSRAM
     * and no business pulling in an allocator. */
    void setTraceBuffers(LapTracePoint *a, LapTracePoint *b, uint16_t capacity);

    /* Points per buffer. At the trace's 5 Hz this covers a 3.4 minute lap,
     * which no kart lap approaches. It follows from that sampling rate, so it
     * lives here rather than at the call sites. */
    static const uint16_t kRecommendedTracePoints = 1024;

    /* Signed ms against the reference lap at the point on track you are at now:
     * negative is up on your best. LAP_NO_DELTA before a reference lap exists.
     * Holds its last value rather than lying when the match drifts too far from
     * the reference path to mean anything: off track, or in the pits. */
    int64_t getLiveDeltaMs() const { return _liveDeltaMs; }

    bool hasReferenceLap() const { return _refCount >= 2; }

    /* The reference lap's own time. Zero until one exists. */
    uint32_t getReferenceLapMs() const;

    /* Where this lap is heading: the reference lap plus the delta being
     * carried. Zero when there is nothing to project from. */
    uint32_t getPredictedLapMs() const;

    /* ---- virtual splits ------------------------------------------------
     * The reference trace is cut into equal slices of reference time, and
     * getSplitDeltaMs() reports the delta accumulated since the current slice
     * began — which is what a delta BAR has to show, because a cumulative one
     * pegs the moment you have a real off and then says nothing about the
     * corners that follow.
     *
     * Virtual rather than gate-driven: the trace already knows where you are on
     * the reference, so this needs no track configuration and works on a track
     * with no split gates set, which is the common case. */
    static const int LAP_VIRTUAL_SPLITS = 8;

    /* Signed ms since the current virtual split opened. LAP_NO_DELTA before a
     * reference exists. */
    int64_t getSplitDeltaMs() const { return _splitDeltaMs; }

    /* Full scale this figure is shown against, either side of zero. Measured
     * on the 12-lap fixture: the split delta stays inside 0.73 s on a normal
     * lap and only 4.4% of samples run past 1.00 s. A property of the number
     * rather than of the widget, so the dash and the harness share it. */
    static const int32_t LAP_BAR_FULLSCALE_MS = 1000;

    /* 0..LAP_VIRTUAL_SPLITS-1 while running, -1 before the first match. */
    int getVirtualSplit() const { return _curVirtualSplit; }

    /* Points in the reference trace, for harness reporting. */
    uint16_t getReferenceCount() const { return _refCount; }

    /* True when the lap just completed became the session best — the signal
     * the dash paints purple. Only meaningful right after processTelemetry()
     * has returned true. */
    bool wasBestLap() const { return _lastLapWasBest; }

    /* ---- sectors ------------------------------------------------------- */
    bool hasSectors() const { return _gateSet[LAP_GATE_S1] && _gateSet[LAP_GATE_S2]; }

    /* 0..2 while running, -1 before the first finish-line crossing. */
    int  getCurrentSector() const { return _currentSector; }

    /* True once a lap is actually being timed. False on the out lap, where the
     * kart may drive through the split gates but is not on a lap. */
    bool isLapUnderWay() const { return currentLapStartTime != 0; }

    /* This lap's split for sector s. 0 until it closes. */
    uint64_t getSectorTime(int s) const;

    /* Signed ms vs the best that sector had stood at before this lap's split —
     * same rule as the lap delta. LAP_SECTOR_NO_DELTA when there is nothing to
     * compare against yet. */
    static const int64_t LAP_SECTOR_NO_DELTA = INT64_MIN;
    int64_t  getSectorDelta(int s) const;

    uint64_t getSectorBest(int s) const;

    /* False when the sector's opening gate was missed this lap, so its split is
     * meaningless. A missed split never affects the lap time, which comes from
     * the finish line alone. */
    bool isSectorValid(int s) const;

    /* Elapsed time in the sector currently being driven, for a live readout. */
    uint64_t getRunningSplitMs(uint64_t nowEpochMs) const;

    /* Elapsed time in the lap being driven, for a live clock. Measured from
     * the same interpolated crossing time the lap times use, so the clock
     * lands exactly on the lap time when the line comes round again. */
    uint64_t getRunningLapMs(uint64_t nowEpochMs) const;

private:
    FinishLine _gate;
    int32_t _deltaLast = 0;
    bool _verbose = true;

    /* Gates, indexed by LAP_GATE_*. Index LAP_GATE_END mirrors _gate. */
    FinishLine _gates[LAP_GATE_COUNT] = {};
    bool       _gateSet[LAP_GATE_COUNT] = { false, false, false };
    uint64_t   _gateLastMs[LAP_GATE_COUNT] = { 0, 0, 0 };
    uint8_t    _gateBackwards[LAP_GATE_COUNT] = { 0, 0, 0 };
    /* Bumped by checkLineCrossing when a crossing is rejected for direction,
     * so the caller can tell "wrong way" from "missed the posts". */
    uint32_t   _backwardsHits = 0;
    bool       _gateWarned[LAP_GATE_COUNT] = { false, false, false };

    int      _currentSector = -1;
    uint64_t _sectorOpenMs  = 0;
    uint64_t _sectorMs      [LAP_GATE_COUNT] = { 0, 0, 0 };
    uint64_t _sectorBestMs  [LAP_GATE_COUNT] = { 0, 0, 0 };
    int64_t  _sectorDeltaMs [LAP_GATE_COUNT] = { 0, 0, 0 };
    bool     _sectorValid   [LAP_GATE_COUNT] = { false, false, false };
    bool     _sectorClosed  [LAP_GATE_COUNT] = { false, false, false };

    void resetSectorsForNewLap();
    void closeSectorAt(int gate, uint64_t crossMs);

    /* ---- live delta, see the public block above ------------------------- */
    LapTracePoint *_ref = nullptr;      /* best lap so far  */
    LapTracePoint *_cur = nullptr;      /* lap in progress  */
    uint16_t _traceCap   = 0;
    uint16_t _refCount   = 0;
    uint16_t _curCount   = 0;
    uint32_t _curLastMs  = 0;           /* elapsed at the last recorded point */
    bool     _curOverflow = false;      /* ran out of buffer: do not promote  */
    int      _matchIdx   = 0;           /* where the last fix matched         */
    /* Metres per 1e-7-degree unit at the reference lap's latitude, so the fix
     * path needs no trigonometry. */
    float    _mPerUnitLat = 0.0f;
    float    _mPerUnitLng = 0.0f;
    int64_t  _liveDeltaMs = LAP_NO_DELTA;
    bool     _lastLapWasBest = false;

    int      _curVirtualSplit  = -1;
    int64_t  _splitEntryDeltaMs = 0;    /* delta as the current slice opened  */
    int64_t  _splitDeltaMs      = LAP_NO_DELTA;

    bool processCrossings(const TelemetryMsg& data);
    void updateTrace(const TelemetryMsg& data);
    void closeTraceAtLine(uint64_t crossMs, double lat, double lng, bool isBest);
    void resetTrace();
    void    matchDelta(double lat, double lng, uint32_t elapsedMs);

    // Tracking the previous point to draw a line segment
    bool _hasLastPoint = false;
    double _lastLat;
    double _lastLng;
    uint64_t _lastTime;

    // Tracking the previous lap time
    uint64_t currentLapStartTime;
    uint64_t previousLapTimeMs;
    uint64_t lastLapTimeMs;
    uint64_t bestLapTimeMs;
    uint64_t previousBestLapTimeMs;
    
    const uint32_t MIN_LAP_TIME_MS = 15000; // Increased to 15s for karts

    bool checkLineCrossing(double Ax, double Ay, double Bx, double By, 
                           double Cx, double Cy, double Dx, double Dy, 
                           double &fraction);
};

#endif