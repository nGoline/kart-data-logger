#include "UbloxGpsProvider.h"

#include <math.h>
#include <time.h>

UbloxGpsProvider::UbloxGpsProvider(int8_t rxPin, int8_t txPin)
    : _rxPin(rxPin), _txPin(txPin), _serialGps(1) {}

uint64_t UbloxGpsProvider::getEpochMs() {
    if (!_gps.date.isValid() || !_gps.time.isValid()) {
        return millis();
    }

    struct tm t;
    t.tm_year = _gps.date.year() - 1900;
    t.tm_mon = _gps.date.month() - 1;
    t.tm_mday = _gps.date.day();
    t.tm_hour = _gps.time.hour();
    t.tm_min = _gps.time.minute();
    t.tm_sec = _gps.time.second();
    t.tm_isdst = 0;

    uint64_t epochSeconds = (uint64_t)mktime(&t);
    return (epochSeconds * 1000ULL) + (_gps.time.centisecond() * 10ULL);
}

bool UbloxGpsProvider::begin() {
    bool foundBaud = false;

    log_i("Testing u-blox connection at 115200 baud...");

    _serialGps.setRxBufferSize(1024);
    _serialGps.begin(115200, SERIAL_8N1, _rxPin, _txPin);

    uint32_t startCheck = millis();
    while (millis() - startCheck < 1500) {
        if (_serialGps.available() > 0 && _serialGps.read() == '$') {
            foundBaud = true;
            log_i("u-blox module detected at 115200 baud.");
            break;
        }
    }

    if (!foundBaud) {
        log_i("No response at 115200. Trying 9600...");
        _serialGps.end();

        _serialGps.setRxBufferSize(1024);
        _serialGps.begin(9600, SERIAL_8N1, _rxPin, _txPin);

        startCheck = millis();
        while (millis() - startCheck < 1500) {
            if (_serialGps.available() > 0 && _serialGps.read() == '$') {
                foundBaud = true;
                log_i("u-blox module detected at 9600 baud. Upgrading to 115200...");

                const uint8_t setBaud115200[] = {
                    0x01,                   // Port ID (UART1)
                    0x00,                   // Reserved
                    0x00, 0x00,             // Reserved
                    0xD0, 0x08, 0x00, 0x00, // UART mode (8N1)
                    0x00, 0xC2, 0x01, 0x00, // Baud rate (115200)
                    0x03, 0x00,             // InProtoMask (UBX+NMEA)
                    0x03, 0x00,             // OutProtoMask (UBX+NMEA)
                    0x00, 0x00,             // Reserved
                    0x00, 0x00              // Reserved
                };
                sendUBXWithChecksum(0x06, 0x00, (uint8_t*)setBaud115200, sizeof(setBaud115200));
                _serialGps.flush();
                delay(200);

                _serialGps.end();
                _serialGps.setRxBufferSize(1024);
                _serialGps.begin(115200, SERIAL_8N1, _rxPin, _txPin);
                break;
            }
        }
    }

    if (!foundBaud) {
        LOG_ERROR("Error: u-blox module not responding at 115200 or 9600 baud.");
        return false;
    }

    log_i("u-blox serial communication established.");

    UBXConfig initial = readCurrentConfig();
    log_d("u-blox BEFORE: Model: %d | Rate: %dms | PerfMode: %d", initial.dynModel, initial.measRate, initial.perfMode);

    if (initial.dynModel != 4 || initial.measRate != kUpdateIntervalMs || initial.perfMode != 0) {
        log_d("u-blox config is not ideal for kart. Applying adjustments...");
        configureUblox();

        delay(200);
        UBXConfig final = readCurrentConfig();
        log_d("u-blox AFTER:  Model: %d | Rate: %dms | PerfMode: %d", final.dynModel, final.measRate, final.perfMode);
    } else {
        log_d("u-blox configuration already optimized for kart.");
    }

    return true;
}

void UbloxGpsProvider::configureUblox() {
    // CFG-RATE: 5Hz (200ms)
    const uint8_t set5Hz[] = {
        (uint8_t)(kUpdateIntervalMs & 0xFF), (uint8_t)((kUpdateIntervalMs >> 8) & 0xFF),// Measurement Rate (200ms = 5Hz)
        0x01, 0x00, // Navigation Rate (1 = output every measurement)
        0x01, 0x00, // Time Reference (1 = GPS time)
    };
    sendUBXWithChecksum(0x06, 0x08, (uint8_t*)set5Hz, sizeof(set5Hz));
    delay(100);

    // CFG-NAV5: Dynamic Model = Automotive (Optimized for accelerations and turns)
    const uint8_t setAutomotive[] = {
        0xFF, 0xFF,             // Mask (DYN + FixMode)
        0x04,                   // Dynamic Model: 4 = Automotive
        0x03,                   // Fix Mode: 3 = Auto 2D/3D
        0x03, 0x20, 0x00, 0x00, // Fixed Altitude 800m (for 2D fix)
        0x45, 0x64, 0x00, 0x01, // Fixed Altitude Variance (8.33m^2)
        0x05,                   // Min Elevation (leave as is)
        0x00,                   // Dead Reckoning Limit (leave as is)
        0xFA, 0x00,             // Position DOP Mask (leave as is)
        0xFA, 0x00,             // Time DOP Mask (leave as is)
        0x64, 0x00,             // Position Accuracy Mask (leave as is)
        0x2C, 0x01,             // Time Accuracy Mask (leave as is)
        0x00,                   // static hold threshold (leave as is)
        0x3C,                   // DGPS Timeout (leave as is)
        0x00, 0x00, 0x00, 0x00, // Reserved (set to 0)
        0x00, 0x00, 0x00, 0x00, // Reserved (set to 0)
        0x00, 0x00, 0x00, 0x00  // Reserved (set to 0)
    };
    sendUBXWithChecksum(0x06, 0x24, (uint8_t*)setAutomotive, sizeof(setAutomotive));
    delay(100);

    // CFG-MSG: Disable Message to free bandwidth
    uint8_t killList[] = { 
        0x01, // GLL
        0x03, // GSV
        0x05, // VTG
        0x08, // ZDA
        0x0A, // DTM
        0x09, // GBS
        0x06, // GRS
        0x07, // GST
        0x41  // TXT
    };

    for (uint8_t id : killList) {
        uint8_t payload[3] = { 0xF0, id, 0x00 }; // Class 0xF0, ID, Rate 0
        sendUBXWithChecksum(0x06, 0x01, payload, 3);
        delay(100);
    }

    uint8_t keepList[] = { 0x00, 0x02, 0x04 }; // GGA, GSA, RMC
    for (uint8_t id : keepList) {
        uint8_t payload[3] = { 0xF0, id, 0x01 }; // Class 0xF0, ID, Rate 1
        sendUBXWithChecksum(0x06, 0x01, payload, 3);
        delay(100);
    }

    const uint8_t setMaxPerf[] = {
        0x08,
        0x00,
    };
    sendUBXWithChecksum(0x06, 0x11, (uint8_t*)setMaxPerf, sizeof(setMaxPerf));

    const uint8_t saveConfigAll[] = {
        0x00, 0x00, 0x00, 0x00,
        0x1F, 0x1F, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x17,
    };
    sendUBXWithChecksum(0x06, 0x09, (uint8_t*)saveConfigAll, sizeof(saveConfigAll));
}

void UbloxGpsProvider::sendUBXWithChecksum(uint8_t msgClass, uint8_t msgID, uint8_t* payload, uint16_t len) {
    uint8_t header[] = { 0xB5, 0x62, msgClass, msgID, (uint8_t)(len & 0xFF), (uint8_t)((len >> 8) & 0xFF) };
    uint8_t ckA = 0;
    uint8_t ckB = 0;

    _serialGps.write(header, 6);
    for (int i = 2; i < 6; i++) {
        ckA += header[i];
        ckB += ckA;
    }

    for (int i = 0; i < len; i++) {
        _serialGps.write(payload[i]);
        ckA += payload[i];
        ckB += ckA;
    }

    _serialGps.write(ckA);
    _serialGps.write(ckB);
}

bool UbloxGpsProvider::pollUBX(uint8_t msgClass, uint8_t msgID, uint8_t* payload, uint8_t payloadLen) {
    uint8_t request[] = { 0xB5, 0x62, msgClass, msgID, 0x00, 0x00, 0x00, 0x00 };

    uint8_t ckA = 0;
    uint8_t ckB = 0;
    for (uint8_t i = 2; i < 6; i++) {
        ckA += request[i];
        ckB += ckA;
    }
    request[6] = ckA;
    request[7] = ckB;

    _serialGps.write(request, sizeof(request));

    uint32_t start = millis();
    int pos = 0;

    while (millis() - start < 1000) {
        if (!_serialGps.available()) {
            continue;
        }

        uint8_t b = _serialGps.read();
        if (pos == 0 && b == 0xB5) pos++;
        else if (pos == 1 && b == 0x62) pos++;
        else if (pos == 2 && b == msgClass) pos++;
        else if (pos == 3 && b == msgID) pos++;
        else if (pos >= 4) {
            if (pos - 6 < payloadLen) {
                payload[pos - 6] = b;
            }
            pos++;
            if (pos >= (payloadLen + 8)) {
                return true;
            }
        } else {
            pos = 0;
        }
    }
    return false;
}

UBXConfig UbloxGpsProvider::readCurrentConfig() {
    UBXConfig cfg = {0, 0, 0};
    uint8_t buffer[64];

    if (pollUBX(0x06, 0x24, buffer, 36)) {
        cfg.dynModel = buffer[2];
    }

    if (pollUBX(0x06, 0x08, buffer, 6)) {
        cfg.measRate = (buffer[1] << 8) | buffer[0];
    }

    if (pollUBX(0x06, 0x11, buffer, 2)) {
        cfg.perfMode = buffer[1];
    }

    return cfg;
}

bool UbloxGpsProvider::update() {
    bool newData = false;
    while (_serialGps.available() > 0) {
        if (_gps.encode(_serialGps.read())) {
            newData = true;
        }
    }
    return newData;
}

double UbloxGpsProvider::getLat() { return _gps.location.lat(); }
double UbloxGpsProvider::getLng() { return _gps.location.lng(); }

double UbloxGpsProvider::getSpeed(float gForce, float gyroZ) {
    double raw = _gps.speed.kmph();

    if (!hasFix()) {
        return 0.0;
    }

    _speedFiltered = _speedAlpha * raw + (1.0 - _speedAlpha) * _speedFiltered;

    if (_speedFiltered >= _minSpeedToMove) {
        if (_moveCounter < _moveCountThreshold) {
            _moveCounter++;
        }
    } else if (_speedFiltered <= _minSpeedToStop) {
        _moveCounter = 0;
    }

    bool gpsMoving = (_moveCounter >= _moveCountThreshold);
    bool imuActive = gForce > _imuDynGStop || fabsf(gyroZ) > _imuGyroZStop;
    bool consideredMoving = gpsMoving || imuActive;

    if (!consideredMoving) {
        _speedFiltered *= 0.5f;
        if (_speedFiltered < 0.5f) {
            _speedFiltered = 0.0f;
        }
        return 0.0;
    }

    // Round to 1 decimal place for clean telemetry logging
    return round(_speedFiltered * 10.0) / 10.0;
}

uint32_t UbloxGpsProvider::getSatellites() { return _gps.satellites.value(); }

bool UbloxGpsProvider::hasFix() {
    const double maxHdop = 3.0;
    const uint32_t minSats = 4;

    if (!_gps.location.isValid()) return false;
    if ((int)_gps.satellites.value() < minSats) return false;
    if (_gps.hdop.isValid() && _gps.hdop.hdop() > maxHdop) return false;

    return true;
}

uint16_t UbloxGpsProvider::getUpdateIntervalMs() const {
    return kUpdateIntervalMs;
}
