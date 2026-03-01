#include "GpsManager.h"

GpsManager::GpsManager(int8_t rxPin, int8_t txPin, uint32_t baud) 
    : _rxPin(rxPin), _txPin(txPin), _baud(baud), _serialGps(1) {}

void GpsManager::begin() {
    _serialGps.setRxBufferSize(1024);
    _serialGps.begin(_baud, SERIAL_8N1, _rxPin, _txPin);
    
    delay(200); 
    configureUblox();
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

// Implementação dos Getters sem 'const'
double GpsManager::getLat() { return _gps.location.lat(); }
double GpsManager::getLng() { return _gps.location.lng(); }
double GpsManager::getSpeed() { return _gps.speed.kmph(); }
uint32_t GpsManager::getSatellites() { return _gps.satellites.value(); }
double GpsManager::getHdop() { return _gps.hdop.hdop(); }

bool GpsManager::hasFix() { 
    return _gps.location.isValid() && _gps.satellites.value() >= 4; 
}

void GpsManager::configureUblox() {
    // 5Hz (200ms)
    const uint8_t set5Hz[] = {
        0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0xC8, 0x00, 
        0x01, 0x00, 0x01, 0x00, 0xDE, 0x6A
    };
    sendUBX(set5Hz, sizeof(set5Hz));
    delay(100);

    // Desativar GSV
    const uint8_t disableGSV[] = {
        0xB5, 0x62, 0x06, 0x01, 0x08, 0x00, 0xF0, 0x03, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x3F
    };
    sendUBX(disableGSV, sizeof(disableGSV));
}

void GpsManager::sendUBX(const uint8_t *msg, size_t len) {
    _serialGps.write(msg, len);
    _serialGps.flush();
}