#include "GpsManager.h"

#if defined(GPS_PROVIDER_ATGM336)
#include "providers/Atgm336GpsProvider.h"
#else
#include "providers/UbloxGpsProvider.h"
#endif

GpsManager::GpsManager(int8_t rxPin, int8_t txPin)
    : _provider(createProvider(rxPin, txPin)) {}

std::unique_ptr<IGpsProvider> GpsManager::createProvider(int8_t rxPin, int8_t txPin) {
#if defined(GPS_PROVIDER_ATGM336)
    return std::unique_ptr<IGpsProvider>(new Atgm336GpsProvider(rxPin, txPin));
#else
    return std::unique_ptr<IGpsProvider>(new UbloxGpsProvider(rxPin, txPin));
#endif
}

bool GpsManager::begin() { return _provider->begin(); }
bool GpsManager::update() { return _provider->update(); }
void GpsManager::end() { _provider->end(); }

double GpsManager::getLat() { return _provider->getLat(); }
double GpsManager::getLng() { return _provider->getLng(); }
double GpsManager::getSpeed(float gForce, float gyroZ) { return _provider->getSpeed(gForce, gyroZ); }
uint32_t GpsManager::getSatellites() { return _provider->getSatellites(); }
uint64_t GpsManager::getEpochMs() { return _provider->getEpochMs(); }
bool GpsManager::hasFix() { return _provider->hasFix(); }
uint16_t GpsManager::getUpdateIntervalMs() const { return _provider->getUpdateIntervalMs(); }
