#include "GpsManager.h"
#include "AudioManager.h"
#include "LapManager.h"

#include <math.h>

GpsManager::GpsManager(int8_t rxPin, int8_t txPin) 
    : _rxPin(rxPin), _txPin(txPin), _serialGps(1) {}

static double deg2rad(double d) { return d * M_PI / 180.0; }
static double rad2deg(double r) { return r * 180.0 / M_PI; }

// Convert GPS date/time (UTC) to Unix epoch seconds.
static uint32_t gpsDateTimeToEpoch(int year, int month, int day, int hour, int minute, int second) {
    // Uses Julian Day conversion
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    int64_t A = year / 100;
    int64_t B = 2 - A + (A / 4);
    int64_t jd = (int64_t)(floor(365.25 * (year + 4716))) + (int64_t)(floor(30.6001 * (month + 1))) + day + B - 1524;
    int64_t epoch = (jd - 2440588LL) * 86400LL + hour * 3600 + minute * 60 + second;
    if (epoch < 0) return 0;
    return (uint32_t)epoch;
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

void GpsManager::startTask(QueueHandle_t telemetryQueue, LapManager* lapTimer, AudioManager* audio) {
    struct Args { GpsManager* self; QueueHandle_t q; LapManager* lap; AudioManager* audio; };
    Args* args = (Args*)pvPortMalloc(sizeof(Args));
    args->self = this; args->q = telemetryQueue; args->lap = lapTimer; args->audio = audio;

    auto taskFn = [](void* p) {
        Args* a = (Args*)p;
        GpsManager* self = a->self;
        QueueHandle_t q = a->q;
        LapManager* lap = a->lap;
        AudioManager* audio = a->audio;
        bool gpsWasFixed = false;
        unsigned long lastWaitAnnounce = 0;

        for (;;) {
            bool hadNew = self->update();
            extern volatile TelemetryData currentData;
            unsigned long now = millis();

            if (hadNew) {
                // publish to shared currentData
                double glat = self->getLat();
                double glng = self->getLng();
                currentData.lat = glat;
                currentData.lng = glng;
                currentData.speed = self->getSpeed();
                currentData.sats = self->getSatellites();
                currentData.hasFix = self->hasFix();
                // Prefer GPS UTC timestamp when available, else fallback to system uptime seconds
                if (self->_gps.time.isValid() && self->_gps.date.isValid()) {
                    int yr = self->_gps.date.year();
                    int mo = self->_gps.date.month();
                    int d = self->_gps.date.day();
                    int hr = self->_gps.time.hour();
                    int mi = self->_gps.time.minute();
                    int se = self->_gps.time.second();
                    uint32_t epoch = gpsDateTimeToEpoch(yr, mo, d, hr, mi, se);
                    if (epoch != 0) currentData.lastUpdate = epoch;
                    else currentData.lastUpdate = now / 1000;
                } else {
                    currentData.lastUpdate = now / 1000;
                }

                // Update bearing when we have a movement vector
                if (self->_lastGpsMillis != 0 && fabs(self->_filteredLat) > 0.0) {
                    double prevLat = self->_filteredLat;
                    double prevLng = self->_filteredLng;
                    double b = self->bearingBetween(prevLat, prevLng, glat, glng);
                    if (!isnan(b)) self->_lastBearingDeg = b;
                }

                // Complementary filter: blend GPS into filtered position
                if (self->_lastGpsMillis == 0) {
                    self->_filteredLat = glat;
                    self->_filteredLng = glng;
                } else {
                    self->_filteredLat = self->_posAlpha * glat + (1.0 - self->_posAlpha) * self->_filteredLat;
                    self->_filteredLng = self->_posAlpha * glng + (1.0 - self->_posAlpha) * self->_filteredLng;
                }
                self->_lastGpsMillis = now;

                // copy to queue (uses filtered position)
                if (q) {
                    TelemetryData sample;
                    memcpy(&sample, (const void*)&currentData, sizeof(sample));
                    sample.lat = self->_filteredLat;
                    sample.lng = self->_filteredLng;
                    sample.lastUpdate = currentData.lastUpdate;
                    xQueueSend(q, &sample, 0);
                }

                if (currentData.hasFix && !gpsWasFixed) {
                    gpsWasFixed = true;
                    if (audio) {
                        audio->tryQueueAudio("/gps.wav");
                        audio->tryQueueAudio("/ready.wav");
                        audio->tryQueueAudio("/silence.wav");
                        audio->tryQueueAudio("/system_ready.wav");
                    }
                }

                if (lap) lap->update();
            }

            if (!gpsWasFixed) {
                unsigned long now2 = millis();
                if (now2 - lastWaitAnnounce >= 10000) {
                    lastWaitAnnounce = now2;
                    if (audio){
                        audio->tryQueueAudio("/wait.wav");
                        audio->tryQueueAudio("/initializing.wav");
                        audio->tryQueueAudio("/gps.wav");
                        audio->tryQueueAudio("/silence.wav");
                    }
                }
            }

            // If GPS data hasn't updated recently, attempt short dead-reckoning
            if (!hadNew) {
                unsigned long since = millis() - self->_lastGpsMillis;
                if (self->_lastGpsMillis != 0 && since <= self->_deadReckonTimeoutMs) {
                    // use currentData.speed and last bearing to advance filtered pos
                    extern volatile TelemetryData currentData;
                    double speed = currentData.speed; // km/h
                    double dt = since / 1000.0; // seconds since last GPS
                    if (speed > 0.5 && !isnan(self->_lastBearingDeg)) {
                        self->deadReckon(self->_lastBearingDeg, speed, dt);
                        // publish a queue sample with dead-reckoned position
                        if (q) {
                            TelemetryData sample;
                            memcpy(&sample, (const void*)&currentData, sizeof(sample));
                            sample.lat = self->_filteredLat;
                            sample.lng = self->_filteredLng;
                            // mark with current UTC seconds if we have one, otherwise system seconds
                            if (self->_gps.time.isValid() && self->_gps.date.isValid()) {
                                int yr = self->_gps.date.year();
                                int mo = self->_gps.date.month();
                                int d = self->_gps.date.day();
                                int hr = self->_gps.time.hour();
                                int mi = self->_gps.time.minute();
                                int se = self->_gps.time.second();
                                sample.lastUpdate = gpsDateTimeToEpoch(yr, mo, d, hr, mi, se);
                            } else {
                                sample.lastUpdate = millis() / 1000;
                            }
                            xQueueSend(q, &sample, 0);
                        }
                    }
                }
            }

            vTaskDelay(pdMS_TO_TICKS(20));
        }
    };

    xTaskCreatePinnedToCore(taskFn, "GPS_Task", 4096, args, 3, NULL, 0);
}

double GpsManager::getLat() { return _gps.location.lat(); }
double GpsManager::getLng() { return _gps.location.lng(); }
double GpsManager::getSpeed() {
    double raw = _gps.speed.kmph();

    // Basic validity checks
    if (!_gps.location.isValid()) return 0.0;
    if ((int)_gps.satellites.value() < 4) return 0.0;

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
    extern volatile TelemetryData currentData;
    const float IMU_DYN_G_STOP = 0.10f; // increased deadband
    const float IMU_GYROZ_STOP = 3.0f;  // degrees/sec
    if (_speedFiltered < 3.0 && fabsf(currentData.gForce) < IMU_DYN_G_STOP && fabsf(currentData.gyroZ) < IMU_GYROZ_STOP) {
        consideredMoving = false;
    }

    // Return smoothed speed only when considered moving
    if (!consideredMoving) return 0.0;

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