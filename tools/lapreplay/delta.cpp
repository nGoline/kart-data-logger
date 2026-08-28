/*
 * Replays a session CSV through the real LapManager and checks the LIVE lap
 * delta: the number the dash hero panel shows continuously, from matching each
 * fix against the reference lap's trace.
 *
 * LapManager.cpp is compiled unmodified against a shimmed Arduino.h, so this
 * exercises the firmware's own matching, not a model of it. The trace buffers
 * are allocated here because the class deliberately allocates nothing: on the
 * dash they come from PSRAM, here from the host heap.
 *
 *   ./delta <csv> <leftLat> <leftLng> <rightLat> <rightLng> [--dump]
 *
 * The check that matters is the err column. At the line, the live delta must
 * agree with the arithmetic one (this lap's time minus the reference lap's)
 * because that is the same comparison arrived at two completely different ways.
 * If the matching is wrong, they diverge.
 */
#include "LapManager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

/* 3.4 minutes of lap at the trace's 5 Hz. An overflow here would mean the
 * recording is not a kart lap. */
#define TRACE_POINTS 1024

/* How far the live delta at the line may sit from the arithmetic one. The last
 * fix lands up to a sample interval before the line, and a metre of position
 * error is about 60 ms at 60 km/h, so some disagreement is physics rather than
 * a bug. Beyond this, the matching is wrong. */
#define LINE_TOLERANCE_MS 250

static void fmt(uint64_t ms, char *out, size_t n) {
    unsigned m = (unsigned)(ms / 60000);
    unsigned s = (unsigned)((ms % 60000) / 1000);
    unsigned f = (unsigned)(ms % 1000);
    if (m) snprintf(out, n, "%u:%02u.%03u", m, s, f);
    else   snprintf(out, n, "%u.%03u", s, f);
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s <csv> <leftLat> <leftLng> <rightLat> <rightLng> [--dump]\n"
                        "  --dump  emit epoch,lap,elapsed_ms,delta_ms per fix on stdout\n",
                argv[0]);
        return 2;
    }
    bool dump = false;
    for (int i = 6; i < argc; i++)
        if (!strcmp(argv[i], "--dump")) dump = true;

    FILE *fp = fopen(argv[1], "r");
    if (!fp) { perror(argv[1]); return 1; }

    FinishLine gate = { atof(argv[2]), atof(argv[3]), atof(argv[4]), atof(argv[5]) };

    LapTracePoint *bufA = new LapTracePoint[TRACE_POINTS];
    LapTracePoint *bufB = new LapTracePoint[TRACE_POINTS];

    LapManager lap;
    lap.setFinishLine(gate);
    lap.setTraceBuffers(bufA, bufB, TRACE_POINTS);

    if (dump) printf("epoch,lap,elapsed_ms,delta_ms\n");
    else {
        printf("lap |     time  | ref | pts | live@line |   arith |    err | moves | held | span | max|split|\n");
        printf("----+-----------+-----+-----+-----------+---------+--------+-------+------+------+-----------\n");
    }

    char line[512];
    if (!fgets(line, sizeof line, fp)) return 1;   // header

    size_t   laps = 0, failures = 0, checked = 0;
    uint64_t refLapMs = 0;          /* lap time of the current reference trace */
    uint64_t lapStart = 0;          /* epoch of the last crossing              */

    /* Per-lap accounting of the live number. */
    int64_t  prevDelta = LapManager::LAP_NO_DELTA;   /* value before this fix  */
    int64_t  lastSeen  = LapManager::LAP_NO_DELTA;
    size_t   moves = 0, held = 0;
    int64_t  dMin = 0, dMax = 0;
    bool     haveSpan = false;
    /* The bar is driven by the split delta, so the property that matters is
     * that it stays inside the bar's scale even on a lap that loses 15 s. A
     * cumulative delta would peg and stay pegged; this must not. */
    int64_t  splitAbsMax = 0;
    size_t   splitOverScale = 0, splitSamples = 0;

    while (fgets(line, sizeof line, fp)) {
        unsigned long long ts;
        double speed, totalG, gx, gy, steer, lat, lng;
        int sats;
        if (sscanf(line, "%llu,%lf,%lf,%lf,%lf,%lf,%d,%lf,%lf",
                   &ts, &speed, &totalG, &gx, &gy, &steer, &sats, &lat, &lng) != 9)
            continue;

        TelemetryMsg m{};
        m.type      = MSG_TELEMETRY;
        m.timestamp = (uint64_t)ts;
        m.speedKmph = (float)speed;
        m.lat = lat; m.lng = lng;
        m.sats = (uint8_t)sats;
        m.hasFix = sats > 0 ? 1 : 0;

        /* The delta as it stood at the previous fix, i.e. the last value the
         * panel showed before the line, which is what the crossing check below
         * compares. processTelemetry() zeroes it for the new lap. */
        prevDelta = lap.getLiveDeltaMs();

        bool crossed = lap.processTelemetry(m);
        int64_t d = lap.getLiveDeltaMs();

        if (!crossed && d != LapManager::LAP_NO_DELTA) {
            if (d == lastSeen) held++;
            else               moves++;
            lastSeen = d;
            if (!haveSpan) { dMin = dMax = d; haveSpan = true; }
            else { if (d < dMin) dMin = d; if (d > dMax) dMax = d; }

            int64_t sd = lap.getSplitDeltaMs();
            if (sd != LapManager::LAP_NO_DELTA) {
                int64_t a = sd < 0 ? -sd : sd;
                if (a > splitAbsMax) splitAbsMax = a;
                splitSamples++;
                if (a > 1000) splitOverScale++;   /* past the bar's full scale */
            }
            if (dump)
                printf("%llu,%zu,%llu,%lld\n", (unsigned long long)m.timestamp, laps,
                       (unsigned long long)(lapStart ? m.timestamp - lapStart : 0),
                       (long long)d);
        }

        if (crossed) {
            laps++;
            uint64_t lapMs = lap.getLastLapTime();
            bool becameRef = lap.wasBestLap();
            uint64_t wasRef = refLapMs;         /* what we were matched against */
            if (becameRef) refLapMs = lapMs;

            if (!dump) {
                if (!lapMs) {
                    printf("%3zu |         - |  -  |   - |         - |       - |      - |"
                           "     - |    - |    -   (first crossing, timing starts here)\n", laps);
                } else {
                    char t[24];
                    fmt(lapMs, t, sizeof t);

                    char liveStr[24] = "        -", arithStr[24] = "      -", errStr[24] = "     -";
                    if (prevDelta != LapManager::LAP_NO_DELTA && wasRef) {
                        int64_t arith = (int64_t)lapMs - (int64_t)wasRef;
                        int64_t err   = prevDelta - arith;
                        snprintf(liveStr,  sizeof liveStr,  "%+8.3f", prevDelta / 1000.0);
                        snprintf(arithStr, sizeof arithStr, "%+7.3f", arith / 1000.0);
                        snprintf(errStr,   sizeof errStr,   "%+6.3f", err / 1000.0);
                        checked++;
                        if (err > LINE_TOLERANCE_MS || err < -LINE_TOLERANCE_MS) failures++;
                    }
                    char spanStr[24] = "    -";
                    if (haveSpan) snprintf(spanStr, sizeof spanStr, "%.2f", (dMax - dMin) / 1000.0);
                    printf("%3zu | %9s | %s | %3u | %9s | %7s | %6s | %5zu | %4zu | %5s | %9.2f\n",
                           laps, t, becameRef ? "NEW" : "   ",
                           (unsigned)lap.getReferenceCount(),
                           liveStr, arithStr, errStr, moves, held, spanStr,
                           splitAbsMax / 1000.0);
                }
            }

            lapStart = m.timestamp;
            moves = held = 0;
            haveSpan = false;
            splitAbsMax = 0;
            lastSeen = LapManager::LAP_NO_DELTA;
        }
    }
    fclose(fp);
    delete[] bufA;
    delete[] bufB;

    if (dump) return 0;

    char b[24];
    fmt(lap.getBestLapTime(), b, sizeof b);
    printf("\ncrossings %zu, best lap %s, reference %u points\n",
           laps, b, (unsigned)lap.getReferenceCount());
    printf("line agreement: %zu lap(s) checked, %zu outside %d ms\n",
           checked, failures, LINE_TOLERANCE_MS);
    printf("split delta:    %zu sample(s), %zu past the bar's 1.00 s full scale (%.1f%%)\n",
           splitSamples, splitOverScale,
           splitSamples ? 100.0 * (double)splitOverScale / (double)splitSamples : 0.0);
    if (!checked) {
        printf("RESULT: nothing to check: no lap ran against a reference\n");
        return 1;
    }
    printf("RESULT: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
