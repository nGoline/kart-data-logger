/*
 * Replays a session CSV through the real LapManager on the host, so finish-line
 * detection can be validated without going to the track.
 *
 * LapManager.cpp is compiled unmodified — only Arduino.h is shimmed.
 *
 *   ./replay <csv> <leftLat> <leftLng> <rightLat> <rightLng>
 */
#include "LapManager.h"
#include "harness.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static double haversine(double lat1, double lon1, double lat2, double lon2) {
    double p = 0.017453292519943295;
    double a = 0.5 - cos((lat2-lat1)*p)/2 +
               cos(lat1*p)*cos(lat2*p)*(1-cos((lon2-lon1)*p))/2;
    return 12742000 * asin(sqrt(a));
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s <csv> <leftLat> <leftLng> <rightLat> <rightLng> [--csv]\n"
                        "  --csv  emit machine-readable crossings on stdout\n", argv[0]);
        return 2;
    }
    // --csv makes this consumable by tools/overlay, so the overlay's lap numbers
    // come from the same LapManager the firmware runs.
    bool asCsv = false;
    for (int i = 6; i < argc; i++)
        if (!strcmp(argv[i], "--csv")) asCsv = true;
    FILE *fp = fopen(argv[1], "r");
    if (!fp) { perror(argv[1]); return 1; }

    FinishLine gate = { atof(argv[2]), atof(argv[3]), atof(argv[4]), atof(argv[5]) };
    double gateLen = haversine(gate.leftLat, gate.leftLng, gate.rightLat, gate.rightLng);
    double cLat = (gate.leftLat + gate.rightLat)/2.0;
    double cLng = (gate.leftLng + gate.rightLng)/2.0;
    if (asCsv) printf("crossing_epoch,lap_index,lap_time_ms,best_ms\n");
    else printf("gate: %.2f m wide, centre %.6f,%.6f\n\n", gateLen, cLat, cLng);

    LapManager lap;
    lap.setFinishLine(gate);

    char line[512];
    if (!fgets(line, sizeof line, fp)) return 1;   // header

    size_t rows = 0, laps = 0, approaches = 0;
    double nearest = 1e18;
    uint64_t firstT = 0, lastT = 0;
    bool wasNear = false;

    TelemetryMsg m;
    while (read_row(fp, m)) {
        if (!rows) firstT = m.timestamp;
        lastT = m.timestamp;
        rows++;

        double d = haversine(m.lat, m.lng, cLat, cLng);
        if (d < nearest) nearest = d;
        bool near = d < 10.0;
        if (near && !wasNear) approaches++;
        wasNear = near;

        if (lap.processTelemetry(m)) {
            laps++;
            uint64_t lastMs = lap.getLastLapTime();
            uint64_t bestMs = lap.getBestLapTime();
            if (bestMs == 0xFFFFFFFFFFFFFFFFULL) bestMs = 0;
            if (asCsv) {
                printf("%llu,%zu,%llu,%llu\n",
                       (unsigned long long)m.timestamp, laps,
                       (unsigned long long)lastMs, (unsigned long long)bestMs);
            } else {
                char a[24], b[24];
                fmt(lastMs, a, sizeof a);
                fmt(bestMs, b, sizeof b);
                double tRel = (m.timestamp - firstT) / 1000.0;
                if (lastMs == 0)
                    printf("  t=%6.1fs  CROSSING %zu  (lap timing starts here)\n", tRel, laps);
                else
                    printf("  t=%6.1fs  CROSSING %zu  lap=%s  best=%s\n", tRel, laps, a, b);
            }
        }
    }
    fclose(fp);

    if (asCsv) return 0;
    printf("\n%zu rows, %.1f s\n", rows, (lastT-firstT)/1000.0);
    printf("closest approach to gate centre: %.1f m\n", nearest);
    printf("times inside the 10 m detection radius: %zu\n", approaches);
    printf("crossings detected: %zu\n", laps);
    if (laps > 1) {
        char b[24]; fmt(lap.getBestLapTime(), b, sizeof b);
        printf("best lap: %s\n", b);
    }
    return 0;
}
