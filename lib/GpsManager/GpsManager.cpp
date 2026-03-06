#include "GpsManager.h"

#include <math.h>
#include <time.h>

GpsManager::GpsManager(int8_t rxPin, int8_t txPin) 
    : _rxPin(rxPin), _txPin(txPin), _serialGps(1) {}

static double deg2rad(double d) { return d * M_PI / 180.0; }
static double rad2deg(double r) { return r * 180.0 / M_PI; }

// Convert GPS date/time (UTC) to Unix epoch milliseconds.
uint64_t GpsManager::getEpochMs() {
    if (!_gps.date.isValid() || !_gps.time.isValid()) return millis(); // Fallback to boot time if no fix

    struct tm t;
    t.tm_year = _gps.date.year() - 1900;
    t.tm_mon  = _gps.date.month() - 1;
    t.tm_mday = _gps.date.day();
    t.tm_hour = _gps.time.hour();
    t.tm_min  = _gps.time.minute();
    t.tm_sec  = _gps.time.second();
    t.tm_isdst = 0; // GPS is always UTC

    // mktime returns seconds since epoch
    uint64_t epochSeconds = (uint64_t)mktime(&t);
    
    // Add centiseconds (1/100th) to get millisecond precision
    return (epochSeconds * 1000ULL) + (_gps.time.centisecond() * 10ULL);
}

void GpsManager::begin() {
    bool foundBaud = false;

    // 1. Tenta primeiro 115200 (Otimizado para o que já está salvo)
    log_i("[GPS] Tentando conexão a 115200 baud...");
    
    _serialGps.setRxBufferSize(1024); // Define ANTES do begin
    _serialGps.begin(115200, SERIAL_8N1, _rxPin, _txPin);
    
    uint32_t startCheck = millis();
    while (millis() - startCheck < 1500) { 
        if (_serialGps.available() > 0) {
            if (_serialGps.read() == '$') { 
                foundBaud = true;
                log_i("[GPS] Módulo detectado a 115200 baud.");
                break;
            }
        }
    }

    // 2. Fallback para 9600 caso o módulo tenha resetado
    if (!foundBaud) {
        log_i("[GPS] Sem resposta a 115200. Tentando 9600...");
        _serialGps.end();
        
        _serialGps.setRxBufferSize(1024); // Define ANTES do begin
        _serialGps.begin(9600, SERIAL_8N1, _rxPin, _txPin);
        
        startCheck = millis();
        while (millis() - startCheck < 1500) {
            if (_serialGps.available() > 0) {
                if (_serialGps.read() == '$') {
                    foundBaud = true;
                    log_i("[GPS] Módulo a 9600. Fazendo upgrade...");
                    
                    // Comando UBX: CFG-PRT para 115200
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
                    _serialGps.setRxBufferSize(1024); // Define ANTES do begin
                    _serialGps.begin(115200, SERIAL_8N1, _rxPin, _txPin);
                    break;
                }
            }
        }
    }

    if (foundBaud) {
        log_i("[GPS] Serial communication established.");

        // checkFirmware();

        UBXConfig initial = readCurrentConfig();
        log_i("BEFORE: Model: %d | Rate: %dms | PerfMode: %d", initial.dynModel, initial.measRate, initial.perfMode);

        if (initial.dynModel != 4 || initial.measRate != 200 || initial.perfMode != 0) {
            log_i("[GPS] Current configuration is not ideal for kart. Applying adjustments...");
            configureUblox();

            delay(200);
            UBXConfig final = readCurrentConfig();
            log_i("AFTER:  Model: %d | Rate: %dms | PerfMode: %d", final.dynModel, final.measRate, final.perfMode);
        } else {
            log_i("[GPS] Configuration already optimized for kart. No changes needed.");
        }
    } else {
        log_e("[GPS] Error: GPS module not responding at 115200 or 9600 baud.");
    }
}

void GpsManager::configureUblox() {
    // CFG-RATE: 5Hz (200ms)
    const uint8_t set5Hz[] = {
        0xC8, 0x00, // Measurement Rate (200ms = 5Hz)
        0x01, 0x00, // Navigation Rate (1 = output every measurement)
        0x01, 0x00, // Time Reference (1 = GPS time)
    };
    sendUBXWithChecksum(0x06, 0x08, (uint8_t*)set5Hz, sizeof(set5Hz));
    delay(100);

    // CFG-NAV5: Dynamic Model = Automotive (Otimizado para acelerações e curvas)
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

    // Ensure the 3 we NEED are running
    uint8_t keepList[] = { 0x00, 0x02, 0x04 }; // GGA, GSA, RMC
    for (uint8_t id : keepList) {
        uint8_t payload[3] = { 0xF0, id, 0x01 };
        sendUBXWithChecksum(0x06, 0x01, payload, 3);
        delay(100);
    }

    // CFG-RXM: Set Max Performance Mode
    const uint8_t setMaxPerf[] = {
        0x08,      // Fixed value
        0x00,      // Max. Performance
    };
    sendUBXWithChecksum(0x06, 0x11, (uint8_t*)setMaxPerf, sizeof(setMaxPerf));

    // CFG-CFG: Save Configuration (Persistir no Flash/EEPROM do GPS)
    const uint8_t saveConfigAll[] = {
        0x00, 0x00, 0x00, 0x00, // Clear mask
        0x1F, 0x1F, 0x00, 0x00, // Save mask (0x1F = All except RINV + ANT)
        0x00, 0x00, 0x00, 0x00, // Load mask
        0x17,                   // Device mask (BBR + Flash + EEPROM + SPI)
    };
    sendUBXWithChecksum(0x06, 0x09, (uint8_t*)saveConfigAll, sizeof(saveConfigAll));
}

void GpsManager::sendUBXWithChecksum(uint8_t msgClass, uint8_t msgID, uint8_t* payload, uint16_t len) {
    uint8_t header[] = { 0xB5, 0x62, msgClass, msgID, (uint8_t)(len & 0xFF), (uint8_t)((len >> 8) & 0xFF) };
    uint8_t ckA = 0, ckB = 0;

    // 1. Send Header & update Checksum (Class through Length)
    _serialGps.write(header, 6);
    for (int i = 2; i < 6; i++) { ckA += header[i]; ckB += ckA; }

    // 2. Send Payload & update Checksum
    for (int i = 0; i < len; i++) {
        _serialGps.write(payload[i]);
        ckA += payload[i];
        ckB += ckA;
    }

    // 3. Send Checksum bytes
    _serialGps.write(ckA);
    _serialGps.write(ckB);
}

// Helper to poll a specific UBX configuration and wait for the response
bool GpsManager::pollUBX(uint8_t msgClass, uint8_t msgID, uint8_t* payload, uint8_t payloadLen) {
    uint8_t request[] = { 0xB5, 0x62, msgClass, msgID, 0x00, 0x00, 0x00, 0x00 };
    
    // Calculate Checksum for the request
    uint8_t ckA = 0, ckB = 0;
    for (uint8_t i = 2; i < 6; i++) {
        ckA += request[i];
        ckB += ckA;
    }
    request[6] = ckA;
    request[7] = ckB;

    _serialGps.write(request, sizeof(request));

    uint32_t start = millis();
    int pos = 0;
    uint8_t sync[2] = {0, 0};

    while (millis() - start < 1000) {
        if (_serialGps.available()) {
            uint8_t b = _serialGps.read();
            if (pos == 0 && b == 0xB5) pos++;
            else if (pos == 1 && b == 0x62) pos++;
            else if (pos == 2 && b == msgClass) pos++;
            else if (pos == 3 && b == msgID) pos++;
            else if (pos >= 4) {
                // We are inside the payload or length header
                // For karts, we'll just skip the length check and fill the user's buffer
                // based on the expected payloadLen of the specific message
                if (pos - 6 < payloadLen) {
                    payload[pos - 6] = b;
                }
                pos++;
                if (pos >= (payloadLen + 8)) return true; 
            } else { pos = 0; }
        }
    }
    return false;
}

UBXConfig GpsManager::readCurrentConfig() {
    UBXConfig cfg = {0, 0, 0};
    uint8_t buffer[64];

    // 1. Poll NAV5 (Dynamic Model)
    if (pollUBX(0x06, 0x24, buffer, 36)) {
        cfg.dynModel = buffer[2]; // Byte 2 of NAV5 payload
        // print all bytes for debugging
        log_d("NAV5 Payload:");
        for (int i = 0; i < 36; i++) {
            log_d("  Byte %d: 0x%02X", i, buffer[i]);
        }
    }

    // 2. Poll RATE (Measurement Rate)
    if (pollUBX(0x06, 0x08, buffer, 6)) {
        cfg.measRate = (buffer[1] << 8) | buffer[0]; // Bytes 0-1 of RATE payload
    }

    // 3. Poll CFG-RXM (Max Performance Mode)
    if (pollUBX(0x06, 0x11, buffer, 2)) {
        cfg.perfMode = buffer[1]; // Byte 1 of RXM payload
    }
    
    return cfg;
}

bool GpsManager::update() {
    bool newData = false;
    while (_serialGps.available() > 0) {
        if (_gps.encode(_serialGps.read())) {
            // if (_gps.location.isUpdated()) {
                newData = true;
            // }
        }
    }
    return newData;
}

double GpsManager::getLat() { return _gps.location.lat(); }
double GpsManager::getLng() { return _gps.location.lng(); }
double GpsManager::getSpeed(float gForce, float gyroZ) {
    double raw = _gps.speed.kmph();

    // Basic validity checks
    if (!_gps.location.isValid() || _gps.satellites.value() < 4) return 0.0;

    // Time for alpha filter (use millis delta to keep stable across callers)
    unsigned long now = millis();
    static unsigned long lastSpeedMillis = 0;
    if (lastSpeedMillis == 0) lastSpeedMillis = now;
    double dt = (now - lastSpeedMillis) / 1000.0;
    if (dt <= 0) dt = 0.02;
    lastSpeedMillis = now;

    // Filter speed (IIR)
    _speedFiltered = _speedAlpha * raw + (1.0 - _speedAlpha) * _speedFiltered;

    // Hysteresis: require several consecutive filtered readings above threshold to mark moving
    if (_speedFiltered >= _minSpeedToMove) {
        _moveCounter = min(_moveCounter + 1, _moveCountThreshold);
    } else if (_speedFiltered <= _minSpeedToStop) {
        _moveCounter = 0;
    }

    // 1. GPS says we are moving based on hysteresis
    bool gpsMoving = (_moveCounter >= _moveCountThreshold);

    // 2. IMU says there is dynamic activity
    const float IMU_DYN_G_STOP = 0.08f; // increased deadband
    const float IMU_GYROZ_STOP = 3.5f;  // degrees/sec
    bool imuActive = gForce > IMU_DYN_G_STOP || fabsf(gyroZ) > IMU_GYROZ_STOP;

    // 3. COMBINE: Consider moving if GPS sees speed OR IMU sees activity
    // This prevents a single low-G sample from zeroing your speed while driving.
    bool consideredMoving = gpsMoving || imuActive;

    // Return smoothed speed only when considered moving
    if (!consideredMoving){
        // IMU says we are still. Slowly bleed off speed to 0 to handle stopping.
        _speedFiltered *= 0.5f; 
        if (_speedFiltered < 0.5f) _speedFiltered = 0.0f;
        return 0.0;
    }

    // Quantize to 0.1 km/h to avoid tiny oscillations in logs
    double quant = round(_speedFiltered * 10.0) / 10.0;
    return quant;
}
uint32_t GpsManager::getSatellites() { return _gps.satellites.value(); }
bool GpsManager::hasFix() {
    // Require valid location, minimum satellites and reasonable HDOP
    const double MAX_HDOP = 3.0; // acceptable horizontal dilution
    if (!_gps.location.isValid()) return false;
    if ((int)_gps.satellites.value() < 4) return false;
    if (_gps.hdop.isValid()) {
        if (_gps.hdop.hdop() > MAX_HDOP) return false;
    }
    return true;
}

double GpsManager::bearingBetween(double lat1, double lon1, double lat2, double lon2) {
    // returns bearing in degrees from lat1/lon1 to lat2/lon2
    double phi1 = deg2rad(lat1);
    double phi2 = deg2rad(lat2);
    double lambda1 = deg2rad(lon1);
    double lambda2 = deg2rad(lon2);
    double y = sin(lambda2 - lambda1) * cos(phi2);
    double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(lambda2 - lambda1);
    double theta = atan2(y, x);
    return fmod((rad2deg(theta) + 360.0), 360.0);
}

void GpsManager::deadReckon(double bearingDeg, double speedKmph, double dtSeconds) {
    // Advance filtered position by distance = speed * dt
    double distanceM = (speedKmph / 3.6) * dtSeconds;
    if (distanceM <= 0.0) return;
    double bearingRad = deg2rad(bearingDeg);
    // approximate meters per degree
    const double metersPerDegLat = 111320.0;
    double latRad = deg2rad(_filteredLat);
    double metersPerDegLon = metersPerDegLat * cos(latRad);
    double dLatDeg = (distanceM * cos(bearingRad)) / metersPerDegLat;
    double dLonDeg = (distanceM * sin(bearingRad)) / metersPerDegLon;
    _filteredLat += dLatDeg;
    _filteredLng += dLonDeg;
}

void GpsManager::checkFirmware() {
    // UBX-MON-VER Poll: B5 62 0A 04 00 00 0E 34
    const uint8_t pollVer[] = { 0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34 };
    
    Serial.println("\n--- [GPS] FIRMWARE IDENTIFICATION (MON-VER) ---");
    _serialGps.write(pollVer, sizeof(pollVer));

    uint32_t start = millis();
    int pos = 0;
    uint16_t payloadLen = 0;
    char buffer[256]; // Store strings for printing

    while (millis() - start < 1500) {
        if (_serialGps.available()) {
            uint8_t b = _serialGps.read();
            
            // Sync with UBX Header
            if (pos == 0 && b == 0xB5) pos++;
            else if (pos == 1 && b == 0x62) pos++;
            else if (pos == 2 && b == 0x0A) pos++; // Class
            else if (pos == 3 && b == 0x04) pos++; // ID
            else if (pos == 4) { payloadLen = b; pos++; }       // Len L
            else if (pos == 5) { payloadLen |= (b << 8); pos++; } // Len H
            else if (pos >= 6 && pos < (6 + payloadLen)) {
                // The response contains multiple 30-byte null-terminated strings
                // 1. Software Version (30 bytes)
                // 2. Hardware Version (10 bytes)
                // 3. Extension Strings (30 bytes each, repeating)
                
                if (b >= 32 && b <= 126) Serial.print((char)b); // Print readable chars
                else if (b == 0) Serial.print(" | ");           // Separator for nulls
                
                pos++;
            }
            else if (pos >= (6 + payloadLen)) {
                Serial.println("\n--- END MON-VER ---");
                return;
            }
        }
    }
    Serial.println("TIMEOUT: Module did not respond to MON-VER.");
}