#include "GpsManager.h"

#include <math.h>
#include <time.h>

GpsManager::GpsManager(int8_t rxPin, int8_t txPin) 
    : _rxPin(rxPin), _txPin(txPin), _serialGps(1) {}

static double deg2rad(double d) { return d * M_PI / 180.0; }
static double rad2deg(double r) { return r * 180.0 / M_PI; }

struct GpsTaskParams {
    GpsManager* instance;
    QueueHandle_t queue;
};

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
    Serial.println("[GPS] Tentando conexão a 115200 baud...");
    
    _serialGps.setRxBufferSize(1024); // Define ANTES do begin
    _serialGps.begin(115200, SERIAL_8N1, _rxPin, _txPin);
    
    uint32_t startCheck = millis();
    while (millis() - startCheck < 1500) { 
        if (_serialGps.available() > 0) {
            if (_serialGps.read() == '$') { 
                foundBaud = true;
                Serial.println("[GPS] Módulo detectado a 115200 baud.");
                break;
            }
        }
    }

    // 2. Fallback para 9600 caso o módulo tenha resetado
    if (!foundBaud) {
        Serial.println("[GPS] Sem resposta a 115200. Tentando 9600...");
        _serialGps.end();
        
        _serialGps.setRxBufferSize(1024); // Define ANTES do begin
        _serialGps.begin(9600, SERIAL_8N1, _rxPin, _txPin);
        
        startCheck = millis();
        while (millis() - startCheck < 1500) {
            if (_serialGps.available() > 0) {
                if (_serialGps.read() == '$') {
                    foundBaud = true;
                    Serial.println("[GPS] Módulo a 9600. Fazendo upgrade...");
                    
                    // Comando UBX: CFG-PRT para 115200
                    const uint8_t setBaud115200[] = {
                        0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 
                        0xD0, 0x08, 0x00, 0x00, 0x00, 0xC2, 0x01, 0x00, 0x07, 0x00, 
                        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x7E
                    };
                    sendUBX(setBaud115200, sizeof(setBaud115200));
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
        Serial.println("[GPS] Comunicação Serial estabelecida.");
        configureUblox();
    } else {
        Serial.println("[GPS] ERRO: GPS não encontrado.");
    }
}

void GpsManager::configureUblox() {
    // CFG-RATE: 5Hz (200ms)
    const uint8_t set5Hz[] = {
        0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0xC8, 0x00, 
        0x01, 0x00, 0x01, 0x00, 0xDE, 0x6A
    };
    sendUBX(set5Hz, sizeof(set5Hz));
    delay(100);

    // CFG-NAV5: Dynamic Model = Automotive (Otimizado para acelerações e curvas)
    const uint8_t setAutomotive[] = {
        0xB5, 0x62, 0x06, 0x24, 0x24, 0x00, 0xFF, 0xFF, 0x04, 0x03, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
        0x00, 0x00, 0x84, 0xEE
    };
    sendUBX(setAutomotive, sizeof(setAutomotive));
    delay(100);

    // CFG-MSG: Desativa GSV (Satélites visíveis) para liberar banda
    const uint8_t disableGSV[] = {
        0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x03, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x3F
    };
    sendUBX(disableGSV, sizeof(disableGSV));
    delay(100);

    // CFG-CFG: Save Configuration (Persistir no Flash/EEPROM do GPS)
    const uint8_t saveConfigAll[] = {
        0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 
        0x00, 0x00, 0x00, 0x00, // Clear mask
        0x1F, 0x1F, 0x00, 0x00, // Save mask (0x1F = Todos os storages)
        0x00, 0x00, 0x00, 0x00, // Load mask
        0x03,                   // Device mask (EEPROM + SPI)
        0x5F, 0x61              // Checksum (Calculado para esta msg)
    };
    sendUBX(saveConfigAll, sizeof(saveConfigAll));
}

void GpsManager::sendUBX(const uint8_t *msg, size_t len) {
    _serialGps.write(msg, len);
}

bool GpsManager::update() {
    bool newData = false;
    while (_serialGps.available() > 0) {
        if (_gps.encode(_serialGps.read())) {
            if (_gps.location.isUpdated()) {
                newData = true;
            }
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

    bool consideredMoving = (_moveCounter >= _moveCountThreshold);

    // Use IMU to suppress GPS micro-movements: if IMU shows no dynamic motion, prefer stopped
    const float IMU_DYN_G_STOP = 0.08f; // increased deadband
    const float IMU_GYROZ_STOP = 3.5f;  // degrees/sec
    consideredMoving = gForce > IMU_DYN_G_STOP || fabsf(gyroZ) > IMU_GYROZ_STOP;

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