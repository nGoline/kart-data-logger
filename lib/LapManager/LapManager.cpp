#include "LapManager.h"

/* How close the kart has to get before the (exact) intersection test is run.
 * Measured to the gate SEGMENT, not its centre: a 20 m gate has a 10 m
 * half-width, so a kart crossing near a post is ~10 m from the centre by
 * definition and a centre-based radius silently drops the crossing. That cost
 * a real lap on a real log. It also matters that the finish line and S2 can be
 * collinear and abutting — segment distance attributes proximity to the right
 * gate where a centre distance cannot. */
#define GATE_RADIUS_M    12.0

/* Minimum spacing between two accepted crossings of the SAME gate. */
#define GATE_COOLDOWN_MS 5000

/* ---- reference trace, for the live delta --------------------------------
 * Sampling rate of the recorded path. The GPS runs at 25 Hz, which would be
 * 1,550 points on a 62 second lap; matching projects onto the segments between
 * points rather than snapping to them, so the delta is not quantised to this
 * rate and 5 Hz costs nothing visible while keeping a lap inside a few KB. */
#define TRACE_INTERVAL_MS  200

/* Points searched either side of the previous match. A few dozen distance
 * computations per fix, and (the real reason) it stops the match jumping to
 * the far side of the track where the circuit passes close to itself. */
#define TRACE_SEARCH_SPAN  15

/* Beyond this from the reference path the match means nothing: an off, a spin,
 * or the pit lane. The delta holds rather than reporting a number produced by
 * matching against a piece of track you are not on. */
#define TRACE_MAX_MATCH_M  25.0

#define TRACE_DEG_SCALE    1e7

void LapManager::setFinishLine(const FinishLine& line) {
    _gate = line;
    _gates[LAP_GATE_END] = line;
    _gateSet[LAP_GATE_END] = true;
    /* A new finish line invalidates the splits — they belong to a track. */
    _gateSet[LAP_GATE_S1] = false;
    _gateSet[LAP_GATE_S2] = false;

    _hasLastPoint = false;
    currentLapStartTime = 0;
    lastLapTimeMs = 0;
    previousLapTimeMs = 0;
    bestLapTimeMs = 0xFFFFFFFFFFFFFFFFULL;
    previousBestLapTimeMs = 0xFFFFFFFFFFFFFFFFULL;

    for (int i = 0; i < LAP_GATE_COUNT; i++) {
        _gateLastMs[i]    = 0;
        _gateBackwards[i] = 0;
        _gateWarned[i]    = false;
        _sectorBestMs[i]  = 0;
    }
    _currentSector = -1;
    resetSectorsForNewLap();
    resetTrace();
    log_i("LapManager: New Finish Line Set.");
}

void LapManager::setSectorGates(const FinishLine *s1, const FinishLine *s2) {
    if (s1) { _gates[LAP_GATE_S1] = *s1; _gateSet[LAP_GATE_S1] = true; }
    if (s2) { _gates[LAP_GATE_S2] = *s2; _gateSet[LAP_GATE_S2] = true; }
    log_i("LapManager: sector gates %s", hasSectors() ? "set" : "incomplete");
}

/* Metres per degree at a given latitude. Equirectangular: over the tens of
 * metres these are used across, the error is far below GPS noise. */
static inline void degToMetres(double atLat, double *kLat, double *kLng) {
    *kLat = 110540.0;
    *kLng = 111320.0 * cos(atLat * 0.017453292519943295);
}

/* Distance from a point to the gate segment, in metres, about the left post. */
static double distToGateSegment(double lat, double lng, const FinishLine &g) {
    double kLat, kLng;
    degToMetres(g.leftLat, &kLat, &kLng);

    double bx = (g.rightLng - g.leftLng) * kLng;
    double by = (g.rightLat - g.leftLat) * kLat;
    double px = (lng - g.leftLng) * kLng;
    double py = (lat - g.leftLat) * kLat;

    double len2 = bx * bx + by * by;
    double t = (len2 > 0.0) ? ((px * bx + py * by) / len2) : 0.0;
    if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;

    double dx = px - t * bx;
    double dy = py - t * by;
    return sqrt(dx * dx + dy * dy);
}

void LapManager::resetSectorsForNewLap() {
    for (int i = 0; i < LAP_GATE_COUNT; i++) {
        _sectorMs[i]     = 0;
        _sectorDeltaMs[i] = LAP_SECTOR_NO_DELTA;
        _sectorValid[i]  = false;
        _sectorClosed[i] = false;
    }
}

/* Close the sector that gate `gate` terminates, if that is the one actually
 * running. If it is not, the sector's opening gate was missed and its split
 * would be nonsense, so it is left invalid rather than recorded wrong. */
void LapManager::closeSectorAt(int gate, uint64_t crossMs) {
    /* Clear the previous lap's splits at S1, not at the finish line. Crossing
     * the line and instantly losing the final sector's delta would throw away
     * the one number you most want to read as you come past the pits; this way
     * the rail holds the completed lap until you reach the first split. */
    if (gate == LAP_GATE_S1) resetSectorsForNewLap();

    /* Nothing counts before the first finish-line crossing. Out of the pits the
     * kart drives through the split gates like any other lap, and without this
     * those partial splits are recorded AND folded into _sectorBestMs, so the
     * first flying lap is measured against a sector containing a pit exit and
     * reads absurdly quick.
     *
     * Safe at the first crossing too: closeSectorAt(END) runs before
     * currentLapStartTime is set, so that call is correctly rejected. */
    if (currentLapStartTime == 0) {
        _currentSector = (gate + 1) % LAP_GATE_COUNT;
        _sectorOpenMs  = crossMs;
        return;
    }

    if (_currentSector == gate && _sectorOpenMs && crossMs > _sectorOpenMs) {
        uint64_t split = crossMs - _sectorOpenMs;
        _sectorMs[gate]    = split;
        _sectorValid[gate] = true;
        _sectorClosed[gate] = true;

        uint64_t prevBest = _sectorBestMs[gate];
        _sectorDeltaMs[gate] = prevBest ? (int64_t)(split - prevBest)
                                        : LAP_SECTOR_NO_DELTA;
        if (!prevBest || split < prevBest) _sectorBestMs[gate] = split;
    } else if (_currentSector >= 0) {
        if (_verbose) log_d("LapManager: sector %d closed out of order — voided", gate);
    }
    /* The next sector always opens here, in order, even after a miss. */
    _currentSector = (gate + 1) % LAP_GATE_COUNT;
    _sectorOpenMs  = crossMs;
}

uint64_t LapManager::getSectorTime(int s) const {
    return (s >= 0 && s < LAP_GATE_COUNT) ? _sectorMs[s] : 0;
}
int64_t LapManager::getSectorDelta(int s) const {
    return (s >= 0 && s < LAP_GATE_COUNT) ? _sectorDeltaMs[s] : LAP_SECTOR_NO_DELTA;
}
uint64_t LapManager::getSectorBest(int s) const {
    return (s >= 0 && s < LAP_GATE_COUNT) ? _sectorBestMs[s] : 0;
}
bool LapManager::isSectorValid(int s) const {
    return (s >= 0 && s < LAP_GATE_COUNT) ? _sectorValid[s] : false;
}
uint64_t LapManager::getRunningSplitMs(uint64_t nowEpochMs) const {
    if (_currentSector < 0 || !_sectorOpenMs || nowEpochMs <= _sectorOpenMs) return 0;
    return nowEpochMs - _sectorOpenMs;
}
uint64_t LapManager::getRunningLapMs(uint64_t nowEpochMs) const {
    if (!currentLapStartTime || nowEpochMs <= currentLapStartTime) return 0;
    return nowEpochMs - currentLapStartTime;
}

// Quick Haversine distance for logging purposes
double getDistance(double lat1, double lon1, double lat2, double lon2) {
    double p = 0.017453292519943295; // Math.PI / 180
    double a = 0.5 - cos((lat2 - lat1) * p)/2 + 
               cos(lat1 * p) * cos(lat2 * p) * (1 - cos((lon2 - lon1) * p))/2;
    return 12742000 * asin(sqrt(a)); // 2 * R; R = 6371 km
}

/* ============================================================================
 * LIVE DELTA
 *
 * The lap in progress is recorded as a polyline of (position, elapsed); the
 * session best is promoted to the reference and every fix matched to the
 * nearest point on it. Indexed by position rather than distance travelled,
 * because distance drifts apart between laps as soon as you take a different
 * line through a corner, whereas the nearest point on the path does not.
 * ========================================================================= */

void LapManager::setTraceBuffers(LapTracePoint *a, LapTracePoint *b, uint16_t capacity) {
    if (!a || !b || capacity < 2) {          /* refuse a half-usable setup */
        _ref = _cur = nullptr;
        _traceCap = 0;
        resetTrace();
        return;
    }
    _ref = a;
    _cur = b;
    _traceCap = capacity;
    resetTrace();
    log_i("LapManager: lap trace enabled, %u points per buffer", capacity);
}

void LapManager::resetTrace() {
    _refCount    = 0;
    _curCount    = 0;
    _curLastMs   = 0;
    _curOverflow = false;
    _matchIdx    = 0;
    _liveDeltaMs = LAP_NO_DELTA;
    _lastLapWasBest = false;
    _curVirtualSplit   = -1;
    _splitEntryDeltaMs = 0;
    _splitDeltaMs      = LAP_NO_DELTA;
}

uint32_t LapManager::getReferenceLapMs() const {
    /* The trace is closed on the line, so its last point carries the lap time. */
    return (_refCount >= 2) ? _ref[_refCount - 1].tMs : 0;
}

uint32_t LapManager::getPredictedLapMs() const {
    if (_refCount < 2 || _liveDeltaMs == LAP_NO_DELTA) return 0;
    int64_t p = (int64_t)getReferenceLapMs() + _liveDeltaMs;
    return (p > 0) ? (uint32_t)p : 0;
}

static inline int32_t traceScale(double deg) {
    return (int32_t)(deg * TRACE_DEG_SCALE + (deg < 0 ? -0.5 : 0.5));
}

void LapManager::closeTraceAtLine(uint64_t crossMs, double lat, double lng, bool isBest) {
    if (!_traceCap) return;

    /* Promote only a lap that was recorded end to end. A lap that outran the
     * buffer, or one whose recording started mid-lap, would match against a
     * path with a hole in it, which is worse than having no reference at all. */
    bool usable = isBest && !_curOverflow && _curCount >= 2 &&
                  currentLapStartTime != 0 && crossMs > currentLapStartTime &&
                  _curCount < _traceCap;

    if (usable) {
        _cur[_curCount++] = { traceScale(lat), traceScale(lng),
                              (uint32_t)(crossMs - currentLapStartTime) };
        LapTracePoint *swap = _ref;
        _ref = _cur;
        _cur = swap;
        _refCount = _curCount;
        /* Metres per scaled unit, for matchDelta. A property of the reference
         * lap rather than of the current fix, so the cos() lands once per lap
         * instead of once per fix. */
        double kLat, kLng;
        degToMetres(_ref[0].lat / TRACE_DEG_SCALE, &kLat, &kLng);
        _mPerUnitLat = (float)(kLat / TRACE_DEG_SCALE);
        _mPerUnitLng = (float)(kLng / TRACE_DEG_SCALE);
        log_i("LapManager: reference lap set: %u points over %llu ms",
              (unsigned)_refCount,
              (unsigned long long)(crossMs - currentLapStartTime));
    }

    /* The new lap's trace starts on the line, at zero. */
    _curCount    = 0;
    _curLastMs   = 0;
    _curOverflow = false;
    _cur[_curCount++] = { traceScale(lat), traceScale(lng), 0 };
    _matchIdx    = 0;
    /* A fresh lap starts level with the reference by definition; the previous
     * lap's value would sit stale on the panel until the first match lands. */
    if (_refCount >= 2) _liveDeltaMs = 0;
    /* -1 so the next match opens slice 0 and takes its baseline. */
    _curVirtualSplit   = -1;
    _splitEntryDeltaMs = 0;
    _splitDeltaMs      = (_refCount >= 2) ? 0 : LAP_NO_DELTA;
}

void LapManager::updateTrace(const TelemetryMsg& data) {
    if (!_traceCap) return;

    /* No lap under way, so nothing to be early or late against. */
    if (currentLapStartTime == 0 || data.timestamp < currentLapStartTime) {
        _liveDeltaMs  = LAP_NO_DELTA;
        _splitDeltaMs = LAP_NO_DELTA;
        return;
    }
    uint32_t elapsed = (uint32_t)(data.timestamp - currentLapStartTime);

    if (_curCount == 0 || elapsed >= _curLastMs + TRACE_INTERVAL_MS) {
        if (_curCount < _traceCap) {
            _cur[_curCount++] = { traceScale(data.lat), traceScale(data.lng), elapsed };
            _curLastMs = elapsed;
        } else if (!_curOverflow) {
            _curOverflow = true;
            log_w("LapManager: lap trace full at %u points, so this lap cannot "
                  "become the delta reference", (unsigned)_traceCap);
        }
    }

    matchDelta(data.lat, data.lng, elapsed);
}

/* Nearest point on the reference polyline, searched in a window around the
 * previous match. Updates _matchIdx and _liveDeltaMs, and leaves both alone
 * when nothing close enough is found. */
void LapManager::matchDelta(double lat, double lng, uint32_t elapsedMs) {
    if (_refCount < 2) {
        _liveDeltaMs  = LAP_NO_DELTA;
        _splitDeltaMs = LAP_NO_DELTA;
        return;
    }

    int last = (int)_refCount - 2;              /* index of the last segment */
    int lo = _matchIdx - TRACE_SEARCH_SPAN;
    int hi = _matchIdx + TRACE_SEARCH_SPAN;
    if (lo < 0) lo = 0;
    if (hi > last) hi = last;

    float bestD2 = 1e30f, bestFrac = 0.0f;
    int   bestI  = -1;

    /* Absolute degrees will not fit in a float — two positions a metre apart
     * differ in the eighth decimal place — but their DIFFERENCES will, and the
     * trace stores scaled integers. So subtract in integers and let only the
     * result be float: 100 m of track is ~9,000 units, three orders inside
     * float's exact range. That leaves no doubles in this loop, which matters
     * because the FPU is single-precision only and this runs thirty-odd times
     * per fix at 25 Hz. The fix is quantised to the trace's 1e-7 degrees. */
    const int32_t fLat = traceScale(lat);
    const int32_t fLng = traceScale(lng);

    for (int i = lo; i <= hi; i++) {
        /* int64 for the subtraction alone: points either side of the
         * antimeridian would overflow int32, which is undefined. */
        float bx = (float)((int64_t)_ref[i + 1].lng - _ref[i].lng) * _mPerUnitLng;
        float by = (float)((int64_t)_ref[i + 1].lat - _ref[i].lat) * _mPerUnitLat;
        float px = (float)((int64_t)fLng - _ref[i].lng) * _mPerUnitLng;
        float py = (float)((int64_t)fLat - _ref[i].lat) * _mPerUnitLat;

        float len2 = bx * bx + by * by;
        float t = (len2 > 0.0f) ? ((px * bx + py * by) / len2) : 0.0f;
        if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;

        float dx = px - t * bx;
        float dy = py - t * by;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) { bestD2 = d2; bestI = i; bestFrac = t; }
    }

    /* Too far from the reference path to mean anything. Hold both the index
     * and the number: an off or a pit stop should freeze the delta, not drag
     * the match to whichever bit of track is nearest. */
    if (bestI < 0 || bestD2 > (float)(TRACE_MAX_MATCH_M * TRACE_MAX_MATCH_M))
        return;

    _matchIdx = bestI;

    /* Interpolate along the matched segment, so the delta is not quantised to
     * TRACE_INTERVAL_MS. */
    uint32_t t0 = _ref[bestI].tMs;
    uint32_t t1 = _ref[bestI + 1].tMs;
    /* Inside float's exact-integer range (16.7M ms is over four hours), so
     * this is millisecond-accurate. */
    float    refMs = (float)t0 + bestFrac * ((float)t1 - (float)t0);

    _liveDeltaMs = (int64_t)((float)elapsedMs - refMs);

    /* Which slice of the reference we are in, by index — the trace is sampled
     * at a fixed interval, so index is proportional to reference time and no
     * division by the lap time is needed. */
    int slice = (int)(((int32_t)bestI * LAP_VIRTUAL_SPLITS) / (int32_t)(_refCount - 1));
    if (slice >= LAP_VIRTUAL_SPLITS) slice = LAP_VIRTUAL_SPLITS - 1;
    if (slice != _curVirtualSplit) {
        /* Re-baseline on entry, including when the match drifts backwards a
         * slice: the bar should read "since here", wherever here now is. */
        _curVirtualSplit   = slice;
        _splitEntryDeltaMs = _liveDeltaMs;
    }
    _splitDeltaMs = _liveDeltaMs - _splitEntryDeltaMs;
}

bool LapManager::processTelemetry(const TelemetryMsg& data) {
    if (!data.hasFix) return false;

    bool lapDone = processCrossings(data);

    /* After the crossings, never before. On a finish crossing the handler has
     * already promoted the finished lap and moved currentLapStartTime, so this
     * sample lands at the head of the NEW lap's trace, which is where it
     * belongs: it was recorded a few tens of ms after the line. */
    updateTrace(data);
    return lapDone;
}

bool LapManager::processCrossings(const TelemetryMsg& data) {
    // We need at least two points to draw a line segment
    if (!_hasLastPoint) {
        _lastLat = data.lat;
        _lastLng = data.lng;
        _lastTime = data.timestamp;
        _hasLastPoint = true;
        return false;
    }

    /* Evaluate every configured gate against this travel segment. The gates are
     * disjoint spans, so at most one can be crossed by a single segment. */
    int    hitGate = -1;
    double fraction = 0.0;

    for (int g = 0; g < LAP_GATE_COUNT; g++) {
        if (!_gateSet[g]) continue;
        if (g != LAP_GATE_END && !hasSectors()) continue;

        double near = distToGateSegment(data.lat, data.lng, _gates[g]);
        if (near >= GATE_RADIUS_M) continue;
        if (_verbose) log_d("Approaching gate %d: %.1fm away", g, near);

        double f = 0.0;
        uint32_t before = _backwardsHits;
        if (checkLineCrossing(_lastLng, _lastLat, data.lng, data.lat,
                              _gates[g].leftLng, _gates[g].leftLat,
                              _gates[g].rightLng, _gates[g].rightLat, f)) {
            hitGate = g; fraction = f;
            _gateBackwards[g] = 0;
            break;
        }
        /* Repeated wrong-way rejections almost always mean the gate's left and
         * right posts are swapped, not that the kart is driving backwards —
         * and the symptom (a gate that simply never fires) points nowhere near
         * the cause, so say so out loud. */
        if (_backwardsHits > before && ++_gateBackwards[g] >= 3 && !_gateWarned[g]) {
            _gateWarned[g] = true;
            log_w("LapManager: gate %d rejected %u crossings as wrong-way — "
                  "are its left/right posts swapped?", g, _gateBackwards[g]);
        }
    }

    // Save current point for the next loop iteration BEFORE we return
    uint64_t timeA = _lastTime;
    double   latA  = _lastLat;
    double   lngA  = _lastLng;
    _lastLat = data.lat;
    _lastLng = data.lng;
    _lastTime = data.timestamp;

    if (hitGate < 0) return false;

    uint64_t crossMs = timeA + (uint64_t)(fraction * (data.timestamp - timeA));

    /* Per-gate cooldown, so one pass cannot register twice. */
    if (_gateLastMs[hitGate] && crossMs > _gateLastMs[hitGate] &&
        (crossMs - _gateLastMs[hitGate]) < GATE_COOLDOWN_MS) return false;

    /* Split gates: close the sector they terminate and open the next. Never
     * touches lap timing — a missed or spurious split cannot corrupt a lap. */
    if (hitGate != LAP_GATE_END) {
        _gateLastMs[hitGate] = crossMs;
        closeSectorAt(hitGate, crossMs);
        return false;
    }

    // Cooldown: Prevent double-triggering if sitting on the start line
    {
        if (data.timestamp - currentLapStartTime > 10000) {
            _gateLastMs[hitGate] = crossMs;
            // INTERPOLATION: Calculate the exact millisecond the kart breached the line
            uint64_t crossingTimeMs = crossMs;

            /* The finish line closes the last sector before it starts a lap. */
            closeSectorAt(LAP_GATE_END, crossingTimeMs);

            _lastLapWasBest = false;
            if (currentLapStartTime != 0) {
                previousLapTimeMs = lastLapTimeMs; // Move current to previous before updating
                lastLapTimeMs = crossingTimeMs - currentLapStartTime;

                // Snapshot the best *before* folding this lap into it, so the
                // dashboard delta can be measured against the time you were
                // actually chasing. Comparing against bestLapTimeMs after the
                // update would read 0.00 on every new personal best.
                previousBestLapTimeMs = bestLapTimeMs;

                // Check for Best Lap
                if (lastLapTimeMs < bestLapTimeMs) {
                    bestLapTimeMs = lastLapTimeMs;
                    _lastLapWasBest = true;
                }
            }

            /* The line itself, interpolated the same way the time was, so the
             * reference trace runs gate to gate: its last point is the line at
             * the lap time, and the next lap's first point is the line at zero.
             * Before currentLapStartTime moves: it is the finished lap's
             * origin, and closeTraceAtLine needs it. */
            closeTraceAtLine(crossingTimeMs,
                             latA + fraction * (data.lat - latA),
                             lngA + fraction * (data.lng - lngA),
                             _lastLapWasBest);

            // Set the start time of the next lap to the exact interpolated crossing time
            currentLapStartTime = crossingTimeMs; 
            return true;
        } else {
            if (_verbose) log_d("Lap crossed but in cooldown. Time since last lap: %llu ms", data.timestamp - currentLapStartTime);
        }
    }
    return false;
}

// 2D Line Segment Intersection Algorithm
bool LapManager::checkLineCrossing(double Ax, double Ay, double Bx, double By, 
                                   double Cx, double Cy, double Dx, double Dy, 
                                   double &fraction) {
    // Vector from A to B (Trajectory)
    double s1_x = Bx - Ax;
    double s1_y = By - Ay;
    
    // Vector from C to D (Finish Line Gate)
    double s2_x = Dx - Cx;
    double s2_y = Dy - Cy;

    double denom = s1_x * s2_y - s2_x * s1_y;
    if (denom == 0) return false; // Lines are parallel

    bool denomPositive = denom > 0;
    double s3_x = Ax - Cx;
    double s3_y = Ay - Cy;

    double s_num = s1_x * s3_y - s1_y * s3_x;
    
    // 1. Did the car cross the infinite line THIS exact frame? (0.0 to 1.0)
    if ((s_num < 0) == denomPositive || (s_num > denom) == denomPositive) {
        return false; // Hasn't reached the line, or already passed it
    }

    // 2. We crossed the line! But was it INSIDE the left/right gate posts?
    double t_num = s2_x * s3_y - s2_y * s3_x;
    if ((t_num < 0) == denomPositive || (t_num > denom) == denomPositive) {
        if (_verbose) log_d("Gate Missed! You crossed the line, but OUTSIDE the Left/Right posts.");
        return false;
    }

    // 3. We are inside the gate! DIRECTION CHECK
    double dotProduct = s1_x * (Cy - Dy) + s1_y * (Dx - Cx);
    if (dotProduct <= 0) {
        _backwardsHits++;   /* caller uses this to spot swapped posts */
        if (_verbose) log_d("Crossed Backwards! Left/Right points are swapped. (IGNORED FOR DESK TEST)");
        return false;
    }

    fraction = s_num / denom; // Exact millisecond percentage
    if (_verbose) log_d("VALID CROSSING! Fraction: %.3f", fraction);
    return true;
}