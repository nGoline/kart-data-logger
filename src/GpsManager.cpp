#include "GpsManager.h"
#include "AudioManager.h"
#include "LapManager.h"

GpsManager::GpsManager(int8_t rxPin, int8_t txPin) 
    : _rxPin(rxPin), _txPin(txPin), _serialGps(1) {}

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
            if (self->update()) {
                // publish to shared currentData
                extern volatile TelemetryData currentData;
                currentData.lat = self->getLat();
                currentData.lng = self->getLng();
                currentData.speed = self->getSpeed();
                currentData.sats = self->getSatellites();
                currentData.hasFix = self->hasFix();
                currentData.lastUpdate = millis();

                // copy to queue
                if (q) {
                    TelemetryData sample;
                    memcpy(&sample, (const void*)&currentData, sizeof(sample));
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
                unsigned long now = millis();
                if (now - lastWaitAnnounce >= 10000) {
                    lastWaitAnnounce = now;
                    if (audio){
                        audio->tryQueueAudio("/wait.wav");
                        audio->tryQueueAudio("/initializing.wav");
                        audio->tryQueueAudio("/gps.wav");
                        audio->tryQueueAudio("/silence.wav");
                    }
                }
            }

            vTaskDelay(pdMS_TO_TICKS(1));
        }
    };

    xTaskCreatePinnedToCore(taskFn, "GPS_Task", 4096, args, 3, NULL, 0);
}

double GpsManager::getLat() { return _gps.location.lat(); }
double GpsManager::getLng() { return _gps.location.lng(); }
double GpsManager::getSpeed() {
    double s = _gps.speed.kmph();
    // Suppress small noisy speeds when fix is poor or stationary
    const double MIN_SPEED_KMPH = 1.0; // below this consider stopped
    if (!_gps.location.isValid()) return 0.0;
    if ((int)_gps.satellites.value() < 4) return 0.0;
    if (s < MIN_SPEED_KMPH) return 0.0;
    return s;
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