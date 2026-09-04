/* Shared by the replay harnesses (replay, sectors, delta, demo): they print
 * lap times so their output compares against each other and against a
 * circuit's official sheet, and three of them read the same log CSV. */
#ifndef HARNESS_H
#define HARNESS_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "EspNowProtocol.h"

/* m:ss.mmm, dropping the leading "0:" under a minute. Not the dash's format
 * (dashFmtTime), which keeps the minute field so the readout cannot change
 * width mid-lap; here the column is already aligned. */
static inline void fmt(uint64_t ms, char *out, size_t n) {
    unsigned m = (unsigned)(ms / 60000);
    unsigned s = (unsigned)((ms % 60000) / 1000);
    unsigned f = (unsigned)(ms % 1000);
    if (m) snprintf(out, n, "%u:%02u.%03u", m, s, f);
    else   snprintf(out, n, "%u.%03u", s, f);
}

/* One log CSV row into a TelemetryMsg. False at EOF or on a row that does not
 * parse — a header line, or a partial final row. */
static inline bool read_row(FILE *fp, TelemetryMsg &m) {
    char line[512];
    while (fgets(line, sizeof line, fp)) {
        /* TelemetryMsg is packed, so scan into aligned locals rather than
         * handing sscanf the addresses of its members. */
        unsigned long long ts;
        double speed, totalG, gx, gy, steer, lat, lng;
        int sats;
        /* epoch,speed,totalGForce,gForceX,gForceY,steering_angle,sats,lat,lng */
        if (sscanf(line, "%llu,%lf,%lf,%lf,%lf,%lf,%d,%lf,%lf",
                   &ts, &speed, &totalG, &gx, &gy, &steer, &sats, &lat, &lng) != 9)
            continue;

        m = TelemetryMsg{};
        m.type          = MSG_TELEMETRY;
        m.timestamp     = (uint64_t)ts;
        m.speedKmph     = (float)speed;
        m.totalGForce   = (float)totalG;
        m.gForceX       = (float)gx;
        m.gForceY       = (float)gy;
        m.lat           = lat;
        m.lng           = lng;
        m.sats          = (uint8_t)sats;
        m.hasFix        = sats > 0 ? 1 : 0;
        m.steeringAngle = (float)steer;
        return true;
    }
    return false;
}

#endif /* HARNESS_H */
