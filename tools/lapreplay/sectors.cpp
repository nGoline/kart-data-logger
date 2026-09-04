/*
 * Replays a session CSV through the real LapManager *with* sector gates, and
 * prints per-lap splits. Same trick as replay.cpp — LapManager.cpp is compiled
 * unmodified against a shimmed Arduino.h — so what this prints is exactly what
 * the firmware computes, not a model of it.
 *
 * Kept separate from replay so tools/overlay's dependency on that stays stable.
 *
 *   ./sectors <csv> \
 *       <endLL> <endLN> <endRL> <endRN> \
 *       <s1LL>  <s1LN>  <s1RL>  <s1RN>  \
 *       <s2LL>  <s2LN>  <s2RL>  <s2RN>
 */
#include "LapManager.h"
#include "harness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char **argv) {
    if (argc < 14) {
        fprintf(stderr, "usage: %s <csv> <end LL LN RL RN> <s1 LL LN RL RN> <s2 LL LN RL RN>\n",
                argv[0]);
        return 2;
    }
    FILE *fp = fopen(argv[1], "r");
    if (!fp) { perror(argv[1]); return 1; }

    FinishLine end = { atof(argv[2]),  atof(argv[3]),  atof(argv[4]),  atof(argv[5])  };
    FinishLine s1  = { atof(argv[6]),  atof(argv[7]),  atof(argv[8]),  atof(argv[9])  };
    FinishLine s2  = { atof(argv[10]), atof(argv[11]), atof(argv[12]), atof(argv[13]) };

    LapManager lap;
    lap.setFinishLine(end);
    lap.setSectorGates(&s1, &s2);
    printf("sectors configured: %s\n\n", lap.hasSectors() ? "yes" : "NO");
    printf("lap |     time  |      S1       S2       S3  | notes\n");
    printf("----+-----------+----------------------------+------\n");

    char line[512];
    if (!fgets(line, sizeof line, fp)) return 1;   // header

    size_t laps = 0;
    TelemetryMsg m;
    while (read_row(fp, m)) {
        if (lap.processTelemetry(m)) {
            uint64_t lastMs = lap.getLastLapTime();
            if (!lastMs) { laps++; continue; }      /* first crossing starts timing */
            laps++;
            char t[24], a[24], b[24], c[24];
            fmt(lastMs, t, sizeof t);
            /* At the finish line, sector 2 has just closed and sectors 0/1 still
             * hold this lap's splits — they are cleared at the next S1. */
            uint64_t s0 = lap.getSectorTime(LAP_GATE_S1);
            uint64_t sA = lap.getSectorTime(LAP_GATE_S2);
            uint64_t sB = lap.getSectorTime(LAP_GATE_END);
            fmt(s0, a, sizeof a); fmt(sA, b, sizeof b); fmt(sB, c, sizeof c);
            bool ok = lap.isSectorValid(LAP_GATE_S1) &&
                      lap.isSectorValid(LAP_GATE_S2) &&
                      lap.isSectorValid(LAP_GATE_END);
            uint64_t sum = s0 + sA + sB;
            printf("%3zu | %9s | %8s %8s %8s | %s%s\n", laps, t, a, b, c,
                   ok ? "" : "SPLIT MISSED  ",
                   (ok && sum) ? (labs((long)(sum - lastMs)) <= 2 ? "sum ok" : "SUM MISMATCH") : "");
        }
    }
    fclose(fp);

    char bb[24];
    fmt(lap.getBestLapTime(), bb, sizeof bb);
    printf("\nbest lap %s   best sectors:", bb);
    for (int i = 0; i < LAP_GATE_COUNT; i++) {
        char x[24]; fmt(lap.getSectorBest(i), x, sizeof x);
        printf("  S%d %s", i + 1, x);
    }
    printf("\n");
    return 0;
}
