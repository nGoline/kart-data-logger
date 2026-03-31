#include "Atgm336GpsProvider.h"

#include <math.h>
#include <time.h>

Atgm336GpsProvider::Atgm336GpsProvider(int8_t rxPin, int8_t txPin)
    : _rxPin(rxPin), _txPin(txPin), _serialGps(1) {}

bool Atgm336GpsProvider::begin() {
    bool foundBaud = false;
    bool hasPendingConfigChanges = false;

    log_i("Testing ATGM336 connection at 115200 baud...");

    _serialGps.setRxBufferSize(1024);
    _serialGps.begin(115200, SERIAL_8N1, _rxPin, _txPin);

    uint32_t startCheck = millis();
    while (millis() - startCheck < 2000) {
        if (_serialGps.available() > 0 && _serialGps.read() == '$') {
            foundBaud = true;
            log_i("ATGM336 module detected at 115200 baud.");
            break;
        }
    }

    if (!foundBaud) {
        log_i("No response at 115200. Trying 9600...");
        _serialGps.end();

        _serialGps.setRxBufferSize(1024);
        _serialGps.begin(9600, SERIAL_8N1, _rxPin, _txPin);

        startCheck = millis();
        while (millis() - startCheck < 2000) {
            if (_serialGps.available() > 0 && _serialGps.read() == '$') {
                foundBaud = true;
                log_i("ATGM336 module detected at 9600 baud. Upgrading to 115200...");

                sendPCAS("01", "5");
                _serialGps.flush();
                delay(200);

                hasPendingConfigChanges = true;

                _serialGps.end();
                _serialGps.setRxBufferSize(1024);
                _serialGps.begin(115200, SERIAL_8N1, _rxPin, _txPin);
                break;
            }
        }
    }

    if (!foundBaud) {
        LOG_ERROR("Error: ATGM336 module not responding at 115200 or 9600 baud.");
        return false;
    }

    log_i("ATGM336 serial communication established.");

    configureAtgm336(hasPendingConfigChanges);

    return true;
}

void Atgm336GpsProvider::end() {
    _serialGps.end();
}

void Atgm336GpsProvider::sendPCAS(char* msgID, char* payload) {
    const char* prefix = "PCAS";
    const char separator = ',';
    uint8_t checksum = 0;

    _serialGps.write("$", 1);
    // 1. XOR and write the prefix ("PCAS")
    for (int i = 0; prefix[i] != '\0'; i++) {
        _serialGps.write(prefix[i]);
        checksum ^= prefix[i];
    }

    // 2. XOR and write the message ID
    for (int i = 0; msgID[i] != '\0'; i++) {
        _serialGps.write(msgID[i]);
        checksum ^= msgID[i];
    }

    // 2. XOR and write the payload
    if (payload[0] != '\0') {
        _serialGps.write(&separator, 1);
        checksum ^= separator;
        for (int i = 0; payload[i] != '\0'; i++) {
            _serialGps.write(payload[i]);
            checksum ^= payload[i];
        }
    }

    // 3. Write the checksum in hex
    _serialGps.write("*", 1);
    if (checksum < 16) {
        _serialGps.write('0');
    }
    _serialGps.print(checksum, HEX);

    // send CRLF
    _serialGps.write("\r\n", 2);
}

void Atgm336GpsProvider::configureAtgm336(bool hasPendingConfigChanges) {
    // Check our message rate by counting how many messages we get in 2 seconds
    int messageCount = 0;
    bool hasIncorrectMessageTypes = false;
    uint32_t startCount = millis();
    while (millis() - startCount < 2000) {
        if (_serialGps.available() > 0) {
            // count only $GNGGA messages
            if (_serialGps.peek() == '$') {
                char buffer[6];
                _serialGps.readBytes(buffer, 6);
                // Check if message is GNGGA, GNGSA, GNRMC, or GNVTG (the messages we expect to be enabled by default)
                // but only count GNGGA for simplicity since it's the most common and we just want a rough message rate estimate
                // If there's a different message comming through set hasIncorrectMessageTypes to true
                if (strncmp(buffer, "$GNGGA", 6) == 0) {
                    messageCount++;
                } else if (strncmp(buffer, "$GNGSA", 6) != 0 && strncmp(buffer, "$GNRMC", 6) != 0 && strncmp(buffer, "$GNVTG", 6) != 0) {
                    hasIncorrectMessageTypes = true;
                }
            } else {
                _serialGps.read(); // discard non-$ bytes
            }
        }
    }
    log_i("Received %d GNGGA messages in 2 seconds.", messageCount);

    if (messageCount < 20) {
        log_i("Configuring ATGM336 for 10Hz update rate...");
        
        // CAS02 Set 10Hz update rate
        sendPCAS("02","100");
        delay(120);
        hasPendingConfigChanges = true;
    }

    if (hasIncorrectMessageTypes) {
        log_i("Unexpected message types detected. Configuring ATGM336 message output...");
        // CAS03 Disable certain messages to reduce load
        // GGA,GLL,GSA,GSV,RMC,VTG,ZDA,ANT,DHV,LPS,res,res,UTC,GST,res,res,res,TIM
        sendPCAS("03","1,0,1,0,1,1,0,0,0,0,,,0,0,,,,0");
        hasPendingConfigChanges = true;
    }

    if (hasPendingConfigChanges) {
        log_i("Saving ATGM336 configuration to flash...");
        // CAS00 Save settings to flash
        sendPCAS("00", "");
        delay(200);
    } else {
        log_i("ATGM336 configuration is already correct. No changes needed.");
    }
}

bool Atgm336GpsProvider::update() {
    bool newData = false;
    while (_serialGps.available() > 0) {
        if (_gps.encode(_serialGps.read())) {
            newData = true;
        }
    }
    return newData;
}

double Atgm336GpsProvider::getLat() { return _gps.location.lat(); }
double Atgm336GpsProvider::getLng() { return _gps.location.lng(); }

double Atgm336GpsProvider::getSpeed(float gForce, float gyroZ) {
    double raw = _gps.speed.kmph();

    if (!hasFix()) {
        return 0.0;
    }

    // --- LOW SPEED / PADDOCK WANDER REJECTION ---
    // If we are crawling, use the IMU to decide if we are actually moving
    if (raw < _minSpeedToMove) {
        bool imuActive = gForce > _imuDynGStop || fabsf(gyroZ) > _imuGyroZStop;
        
        if (!imuActive) {
            // Force zero. We are sitting in the pits.
            _speedFiltered = 0.0;
            _moveCounter = 0;
            return 0.0; 
        }
    }

    // --- HIGH SPEED / TRACK MODE ---
    // At track speeds, raw VTG doppler speed is cleaner than an EMA filter.
    _speedFiltered = raw; 

    // Round to 1 decimal place for clean telemetry logging
    return round(_speedFiltered * 10.0) / 10.0;
}

uint32_t Atgm336GpsProvider::getSatellites() { return _gps.satellites.value(); }

uint64_t Atgm336GpsProvider::getEpochMs() {
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

    const uint64_t epochSeconds = (uint64_t)mktime(&t);
    return (epochSeconds * 1000ULL) + (_gps.time.centisecond() * 10ULL);
}

bool Atgm336GpsProvider::hasFix() {
    const double maxHdop = 1.5;
    const uint32_t minSats = 6;

    if (!_gps.location.isValid()) return false;
    if ((int)_gps.satellites.value() < minSats) return false;
    if (_gps.hdop.isValid() && _gps.hdop.hdop() > maxHdop) return false;

    return true;
}

uint16_t Atgm336GpsProvider::getUpdateIntervalMs() const {
    return kUpdateIntervalMs;
}
