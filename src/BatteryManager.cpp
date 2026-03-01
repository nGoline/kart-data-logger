#include "BatteryManager.h"

BatteryManager::BatteryManager(uint8_t pin) : _pin(pin) {}

void BatteryManager::begin() {
    pinMode(_pin, INPUT);
    // Para o Arduino Core 3.0, a configuração padrão de 12 bits e atenuação costuma bastar.
}

float BatteryManager::getVoltage() const {
    uint32_t raw = analogRead(_pin);
    // Fórmula padrão do divisor do XIAO ESP32-S3:
    // (Leitura * Tensão Referência / Resolução ADC) * 2 (por causa do divisor resistivo interno)
    return (raw * 3.3 / 4096.0) * 2.0; 
}

uint8_t BatteryManager::getPercentage() const {
    float voltage = getVoltage();
    // Curva simples de uma bateria Li-Po (3.2V = 0%, 4.2V = 100%)
    if (voltage >= 4.2f) return 100;
    if (voltage <= 3.2f) return 0;
    return (uint8_t)((voltage - 3.2f) / 1.0f * 100.0f);
}