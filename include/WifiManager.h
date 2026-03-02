#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include "AudioManager.h"
#include "ArduinoOTA.h"
#include "LittleFS.h"
#include <FS.h>

class WifiManager {
public:
    WifiManager(const char* ssid, const char* pass);
    bool begin();
    void startServices();
    void setTelemetryQueue(QueueHandle_t q);
    void broadcast(const char* msg);

private:
    const char* _ssid;
    const char* _pass;
    WebServer _httpServer{80};
    WiFiServer _telnetServer{23};
    QueueHandle_t _telemetryQueue = NULL;
    static const int MAX_TELNET_CLIENTS = 3;
    WiFiClient _clients[MAX_TELNET_CLIENTS];

    void setupHttpRoutes();
    static void telnetTask(void*);
};

#endif
