#include "ReplayManager.h"

bool ReplayManager::begin(fs::FS &fs, const char *path, float rate,
                          bool loop, uint32_t skipS) {
    _rate   = rate;
    _loop   = loop;
    _skipMs = skipS * 1000UL;

    _f = fs.open(path, FILE_READ);
    if (!_f) {
        log_e("Replay: cannot open %s", path);
        return false;
    }

    /* Drop the CSV header. */
    if (_f.available()) _f.readStringUntil('\n');

    _finished       = false;
    _anchored       = false;
    _havePending    = false;
    _rows           = 0;
    _haveFileStart  = false;
    _skipDone       = (_skipMs == 0);

    log_i("Replay: %s open, rate=%.1fx%s, skip=%lus", path,
          (double)_rate, _loop ? ", looping" : "", (unsigned long)(_skipMs / 1000));
    return true;
}

void ReplayManager::restart() {
    _f.seek(0);
    if (_f.available()) _f.readStringUntil('\n');   /* header again */
    _anchored = false;                              /* re-anchor the pacing clock */
    _skipDone = (_skipMs == 0);                     /* skip the lead-in again too */
    log_i("Replay: looping (%lu rows played)", (unsigned long)_rows);
}

bool ReplayManager::readRow(TelemetryMsg &msg) {
    while (_f.available()) {
        String line = _f.readStringUntil('\n');
        if (line.length() < 20) continue;           /* blank / truncated tail */

        unsigned long long epoch = 0;
        float  speed = 0, total = 0, gx = 0, gy = 0, steer = 0;
        int    sats  = 0;
        double lat   = 0, lng = 0;

        /* epoch,speed,totalGForce,gForceX,gForceY,steering_angle,sats,lat,lng */
        int parsed = sscanf(line.c_str(), "%llu,%f,%f,%f,%f,%f,%d,%lf,%lf",
                            &epoch, &speed, &total, &gx, &gy, &steer,
                            &sats, &lat, &lng);
        if (parsed != 9) continue;                  /* skip anything malformed */

        msg.type          = MSG_TELEMETRY;
        msg.timestamp     = (uint64_t)epoch;
        msg.speedKmph     = speed;
        msg.totalGForce   = total;
        msg.gForceX       = gx;
        msg.gForceY       = gy;
        msg.gyroZ         = 0.0f;                   /* not present in the log */
        msg.steeringAngle = steer;
        msg.sats          = (uint8_t)sats;
        msg.lat           = lat;
        msg.lng           = lng;
        msg.hasFix        = (sats >= 3) ? 1 : 0;
        return true;
    }
    return false;   /* EOF */
}

bool ReplayManager::update(TelemetryMsg &msg) {
    if (!_f || _finished) return false;

    if (!_havePending) {
        /* Discard at most a bounded number of rows per call while seeking past
         * the lead-in, so a long skip cannot stall loop() into the task
         * watchdog. A 3-minute skip is ~3300 rows: ~17 calls, imperceptible. */
        const int MAX_DISCARD_PER_CALL = 200;
        int discarded = 0;

        for (;;) {
            if (!readRow(_pending)) {
                if (!_loop) {
                    _finished = true;
                    log_i("Replay: end of file, %lu rows played", (unsigned long)_rows);
                    return false;
                }
                restart();
                if (!readRow(_pending)) {   /* empty or header-only file */
                    _finished = true;
                    log_w("Replay: no usable rows");
                    return false;
                }
            }

            if (!_haveFileStart) {
                _fileStart     = _pending.timestamp;
                _haveFileStart = true;
            }

            if (_skipDone) break;

            uint64_t offset = (_pending.timestamp > _fileStart)
                            ? (_pending.timestamp - _fileStart) : 0;
            if (offset >= (uint64_t)_skipMs) {
                _skipDone = true;
                log_i("Replay: skipped lead-in to t=%lus", (unsigned long)(offset / 1000));
                break;
            }

            if (++discarded >= MAX_DISCARD_PER_CALL) return false;  /* resume next call */
        }
        _havePending = true;
    }

    /* Anchor the pacing clock to the first row we emit, and re-anchor after a
     * loop so the second pass is not instantly "overdue". */
    if (!_anchored) {
        _firstEpoch = _pending.timestamp;
        _startMs    = millis();
        _anchored   = true;
    }

    if (_rate > 0.0f) {
        uint32_t elapsed   = millis() - _startMs;
        uint64_t virtualMs = (uint64_t)((double)elapsed * (double)_rate);
        uint64_t offset    = (_pending.timestamp > _firstEpoch)
                           ? (_pending.timestamp - _firstEpoch) : 0;
        if (virtualMs < offset) return false;       /* not due yet */
    }

    msg = _pending;
    _havePending = false;
    _rows++;

    /* Heartbeat: makes "nothing is happening" diagnosable — a stationary kart
     * in the pits and a replay that never started look identical on the dash. */
    static uint32_t lastBeatMs = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastBeatMs >= 5000) {
        lastBeatMs = nowMs;
        uint64_t offset = (msg.timestamp > _fileStart) ? (msg.timestamp - _fileStart) : 0;
        log_i("Replay: t=%lus  %lu rows  %.1f km/h  sats=%u",
              (unsigned long)(offset / 1000), (unsigned long)_rows,
              (double)msg.speedKmph, (unsigned)msg.sats);
    }
    return true;
}
