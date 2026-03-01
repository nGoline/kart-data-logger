#include "StorageManager.h"

StorageManager::StorageManager() : _lastFlushTime(0) {}

bool StorageManager::begin() {
    // Tenta montar o LittleFS. Se falhar (ex: primeiro boot), ele formata a partição (true)
    if (!LittleFS.begin(true)) { 
        return false;
    }
    
    // Abre em modo APPEND para não sobrescrever o arquivo ao reiniciar o ESP32
    _logFile = LittleFS.open("/telemetry.csv", FILE_APPEND);
    if (!_logFile) {
        return false;
    }

    // Se o arquivo for novo (tamanho 0), escreve o cabeçalho do CSV
    if (_logFile.size() == 0) {
        _logFile.println("Timestamp,Latitude,Longitude,Speed_kmh,Battery_V");
        _logFile.flush();
    }
    return true;
}

void StorageManager::logData(uint32_t timestamp, double lat, double lng, double speed, float batVolts) {
    if (!_logFile) return;

    _logFile.printf("%lu,%.6f,%.6f,%.2f,%.2f\n", timestamp, lat, lng, speed, batVolts);

    // Gerencia o flush automaticamente para garantir integridade caso a energia caia
    if (millis() - _lastFlushTime > FLUSH_INTERVAL_MS) {
        flush();
    }
}

void StorageManager::flush() {
    if (_logFile) {
        _logFile.flush();
        _lastFlushTime = millis();
    }
}