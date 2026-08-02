#ifndef REPLAY_MANAGER_H
#define REPLAY_MANAGER_H

#include <Arduino.h>
#include <FS.h>
#include "EspNowProtocol.h"

/* ============================================================================
 * ReplayManager — feed a recorded session CSV back through the live telemetry
 * path so lap detection, best lap and the delta pill can be exercised on the
 * bench, without a kart, a track or a GPS fix.
 *
 * Reads the exact format LogManager writes to the SD card:
 *
 *   epoch,speed,totalGForce,gForceX,gForceY,steering_angle,sats,lat,lng
 *
 * Timing note: LapManager derives every lap time from TelemetryMsg.timestamp
 * (the GPS epoch in the file) and never from millis() — it even interpolates
 * the line-crossing instant between samples. So lap, best and delta values come
 * out identical no matter how fast the file is played back. The rate control
 * below only affects how the dashboard *looks* while it runs; use 1.0 to watch
 * it honestly, or a large value to check a whole session's lap times in
 * seconds.
 * ========================================================================= */

class ReplayManager {
public:
    /* path:   CSV on the SD card or LittleFS, e.g. "/log_3.csv".
     * rate:   playback multiplier — 1.0 real time, 10.0 ten times faster,
     *         0 (or less) emits every row as fast as loop() will take them.
     * loop:   restart from the top on EOF, so the dash keeps cycling.
     * skipS:  seconds of the file to discard from the start. Real sessions open
     *         with minutes of standing still in the pits, which looks exactly
     *         like a dead dashboard — skip past it to land near the action. */
    bool begin(fs::FS &fs, const char *path, float rate = 1.0f,
               bool loop = true, uint32_t skipS = 0);

    /* Fills msg and returns true when the next sample is due. Non-blocking:
     * returns false while waiting for the pacing clock. */
    bool update(TelemetryMsg &msg);

    bool     isOpen()     const { return (bool)_f; }
    bool     finished()   const { return _finished; }
    uint32_t rowsPlayed() const { return _rows; }

private:
    bool readRow(TelemetryMsg &msg);
    void restart();

    File     _f;
    float    _rate      = 1.0f;
    bool     _loop      = true;
    bool     _finished  = false;

    /* Pacing anchors: file epoch and wall clock at the first emitted row. */
    uint64_t _firstEpoch = 0;
    uint32_t _startMs    = 0;
    bool     _anchored   = false;

    /* Lead-in skip, measured from the first epoch in the file. */
    uint32_t _skipMs        = 0;
    uint64_t _fileStart     = 0;
    bool     _haveFileStart = false;
    bool     _skipDone      = false;

    TelemetryMsg _pending    = {};
    bool         _havePending = false;
    uint32_t     _rows        = 0;
};

#endif // REPLAY_MANAGER_H
