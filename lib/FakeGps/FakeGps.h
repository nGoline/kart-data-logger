#include <FS.h>
#include <LittleFS.h>
#include "EspNowProtocol.h"

class FakeGps {
public:
    bool begin(const char* path) {
        _file = LittleFS.open(path, "r");
        if (!_file) {
            log_e("Failed to open replay file!");
            return false;
        }

        // Skip the header line
        if (_file.available()) _file.readStringUntil('\n');
        return true;
    }

    // Call this in your main loop instead of the real GPS read
    bool update(TelemetryMsg &msg) {
        if (!_file || !_file.available()) {
            _file.seek(0); // Loop the track for continuous testing
            _file.readStringUntil('\n'); // Skip header again
            log_i("Looping replay file...");
        }

        String line = _file.readStringUntil('\n');
        if (line.length() < 30) return false; // Sanity check for empty/short lines
        // Using sscanf to handle the comma-separated variable speed 
        // while the fixed-length trailing bits are handled by the format string
        long long epoch;
        float speed, gForce;
        int sats;
        double lat, lng;

        // Format: epoch,speed,gForce,sats,lat,lng
        int parsed = sscanf(line.c_str(), "%lld,%f,%f,%d,%lf,%lf", 
                            &epoch, &speed, &gForce, &sats, &lat, &lng);

        if (parsed == 6) {
            msg.type = MSG_TELEMETRY;
            msg.timestamp = (uint64_t)epoch;
            msg.speedKmph = speed;
            msg.totalGForce = gForce;
            msg.gForceX = gForce * 0.6; // Simulate some lateral Gs
            msg.gForceY = gForce * 0.8; // Simulate some longitudinal
            msg.sats = (uint8_t)sats;
            msg.lat = lat;
            msg.lng = lng;
            msg.hasFix = (sats >= 3);
            msg.helmetBattery = 100;

            return true;
        }
        return false;
    }

private:
    File _file;
};