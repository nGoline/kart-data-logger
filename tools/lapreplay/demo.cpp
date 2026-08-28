/*
 * Runs lib/DemoTrack through the real LapManager on the host, which is the
 * only practical way to know DEMO mode will actually do anything on the dash.
 *
 * The failure this exists to catch: a gate whose left and right posts are the
 * wrong way round is rejected by LapManager's direction test, and the symptom
 * is not an error, it is a lap counter that sits at zero forever while
 * everything else looks healthy. Same for a gate the synthetic path misses.
 *
 *   ./demo [laps]
 */
#include "DemoTrack.h"
#include "LapManager.h"

#include <cstdio>
#include <cstdlib>

#define TRACE_POINTS 1024

static void fmt(uint64_t ms, char *out, size_t n) {
    unsigned m = (unsigned)(ms / 60000);
    unsigned s = (unsigned)((ms % 60000) / 1000);
    unsigned f = (unsigned)(ms % 1000);
    if (m) snprintf(out, n, "%u:%02u.%03u", m, s, f);
    else   snprintf(out, n, "%u.%03u", s, f);
}

int main(int argc, char **argv) {
    int wantLaps = (argc > 1) ? atoi(argv[1]) : 8;
    if (wantLaps < 2) wantLaps = 2;

    DemoTrack demo;
    demo.begin(1785000000000ULL);

    LapManager lap;
    FinishLine g[DemoTrack::GATE_COUNT];
    for (int i = 0; i < DemoTrack::GATE_COUNT; i++)
        demo.gate(i, g[i].leftLat, g[i].leftLng, g[i].rightLat, g[i].rightLng);

    lap.setFinishLine(g[DemoTrack::GATE_END]);
    lap.setSectorGates(&g[DemoTrack::GATE_S1], &g[DemoTrack::GATE_S2]);

    LapTracePoint *a = new LapTracePoint[TRACE_POINTS];
    LapTracePoint *b = new LapTracePoint[TRACE_POINTS];
    lap.setTraceBuffers(a, b, TRACE_POINTS);

    printf("gates (left lat/lng -> right lat/lng):\n");
    const char *tag[] = { "S1 ", "S2 ", "END" };
    for (int i = 0; i < DemoTrack::GATE_COUNT; i++)
        printf("  %s  %.7f,%.7f -> %.7f,%.7f\n", tag[i],
               g[i].leftLat, g[i].leftLng, g[i].rightLat, g[i].rightLng);

    printf("\nlap |     time  | ref |       S1       S2      END | sum | live@line\n");
    printf("----+-----------+-----+----------------------------+-----+----------\n");

    uint32_t wall = 0;
    size_t   laps = 0, splitMisses = 0, sumBad = 0;
    uint64_t firstCross = 0, lastCross = 0;
    int64_t  prevDelta = LapManager::LAP_NO_DELTA;
    double   vMin = 1e9, vMax = -1e9;

    /* Two minutes of headroom per expected lap: if crossings are not being
     * detected this must terminate rather than spin. */
    const uint32_t maxSteps = (uint32_t)wantLaps * (120000u / DemoTrack::STEP_MS) + 1000u;

    for (uint32_t n = 0; n < maxSteps && (int)laps <= wantLaps; n++) {
        wall += DemoTrack::STEP_MS;
        TelemetryMsg m{};
        if (!demo.update(wall, m)) continue;

        if (m.speedKmph < vMin) vMin = m.speedKmph;
        if (m.speedKmph > vMax) vMax = m.speedKmph;

        prevDelta = lap.getLiveDeltaMs();
        if (!lap.processTelemetry(m)) continue;

        laps++;
        if (!firstCross) firstCross = m.timestamp;
        lastCross = m.timestamp;

        uint64_t lapMs = lap.getLastLapTime();
        if (!lapMs) { printf("%3zu |         - |  -  | (first crossing, timing starts here)\n", laps); continue; }

        uint64_t s0 = lap.getSectorTime(LAP_GATE_S1);
        uint64_t s1 = lap.getSectorTime(LAP_GATE_S2);
        uint64_t s2 = lap.getSectorTime(LAP_GATE_END);
        bool ok = lap.isSectorValid(LAP_GATE_S1) && lap.isSectorValid(LAP_GATE_S2) &&
                  lap.isSectorValid(LAP_GATE_END);
        if (!ok) splitMisses++;
        bool sum_ok = ok && labs((long)((s0 + s1 + s2) - lapMs)) <= 2;
        if (ok && !sum_ok) sumBad++;

        char t[24], x0[24], x1[24], x2[24], live[24] = "        -";
        fmt(lapMs, t, sizeof t);
        fmt(s0, x0, sizeof x0); fmt(s1, x1, sizeof x1); fmt(s2, x2, sizeof x2);
        if (prevDelta != LapManager::LAP_NO_DELTA)
            snprintf(live, sizeof live, "%+8.3f", prevDelta / 1000.0);

        printf("%3zu | %9s | %s | %8s %8s %8s | %s | %9s\n",
               laps, t, lap.wasBestLap() ? "NEW" : "   ", x0, x1, x2,
               ok ? (sum_ok ? " ok" : "BAD") : "MISS", live);
    }

    delete[] a; delete[] b;

    char best[24]; fmt(lap.getBestLapTime(), best, sizeof best);
    printf("\ncrossings %zu, best %s, speed %.0f-%.0f km/h, reference %u points\n",
           laps, laps > 1 ? best : "-", vMin, vMax, (unsigned)lap.getReferenceCount());
    if (laps > 1)
        printf("wall time simulated: %.1f s over %zu timed lap(s)\n",
               (lastCross - firstCross) / 1000.0, laps - 1);

    bool pass = ((int)laps >= wantLaps) && !splitMisses && !sumBad && lap.hasReferenceLap();
    if ((int)laps < wantLaps)
        printf("FAIL: only %zu crossings in %u steps — gates missed, or posts swapped\n",
               laps, maxSteps);
    if (splitMisses) printf("FAIL: %zu lap(s) with a missed split\n", splitMisses);
    if (sumBad)      printf("FAIL: %zu lap(s) where the splits do not sum to the lap\n", sumBad);
    if (!lap.hasReferenceLap()) printf("FAIL: no reference lap was ever promoted\n");
    printf("RESULT: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
