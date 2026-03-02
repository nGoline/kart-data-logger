#include "WifiManager.h"

WifiManager::WifiManager(const char* ssid, const char* pass) : _ssid(ssid), _pass(pass) {}

bool WifiManager::begin() {
    WiFi.begin(_ssid, _pass);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(200);
        Serial.print('.');
    }
    if (WiFi.status() != WL_CONNECTED) return false;

    Serial.println("\n[WiFi] Conectado!");
    Serial.print("[WiFi] IP: ");
    Serial.println(WiFi.localIP());

    // HTTP server routes use LittleFS
    setupHttpRoutes();
    _httpServer.begin();

    // OTA init
    ArduinoOTA.setHostname("kart-data-logger");
    ArduinoOTA.setPassword("kart-data-logger-ota");
    ArduinoOTA.onStart([this](){ Serial.println("[OTA] Start"); });
    ArduinoOTA.onEnd([this](){ Serial.println("[OTA] End"); });
    ArduinoOTA.onProgress([this](unsigned int p, unsigned int t){ char b[32]; snprintf(b,sizeof(b),"[OTA] %u%%", (unsigned int)((p*100)/t)); Serial.println(b); });
    ArduinoOTA.onError([this](ota_error_t err){ Serial.printf("[OTA] Error %u\n", err); });
    ArduinoOTA.begin();

    // Start OTA handler task - use a larger stack and don't pin to a core
    // Give OTA a higher priority so it can process incoming packets reliably
    xTaskCreate([](void*){
        for (;;) {
            ArduinoOTA.handle();
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }, "OTA_Task", 8192, NULL, 3, NULL);

    // start telnet manager task
    xTaskCreatePinnedToCore(WifiManager::telnetTask, "TelnetTask", 4096, this, 1, NULL, 1);

    Serial.println("[HTTP] Log server available at /logs");
    return true;
}

void WifiManager::setTelemetryQueue(QueueHandle_t q) {
    _telemetryQueue = q;
}

void WifiManager::startServices() {
    // nothing for now; begin() did necessary setup
}

void WifiManager::setupHttpRoutes() {
    // list logs
    _httpServer.on("/logs", HTTP_GET, [&]() {
        String html = "<html><head><meta charset=\"utf-8\"><title>Logs</title></head><body><h1>Telemetry Logs</h1><ul>";
        File root = LittleFS.open("/");
        if (root) {
            File file = root.openNextFile();
            while (file) {
                String name = file.name();
                size_t size = file.size();
                if (name.startsWith("/telemetry_") && name.endsWith(".csv")) {
                    html += "<li><a href=\"/logs" + name + "\">" + name + "</a> (" + String(size) + " bytes)</li>";
                }
                file = root.openNextFile();
            }
            root.close();
        }
        html += "</ul></body></html>";
        _httpServer.send(200, "text/html", html);
    });

    _httpServer.onNotFound([&]() {
        String uri = _httpServer.uri();
        if (uri.startsWith("/logs/")) {
            String fname = uri.substring(5); // strip '/logs'
            if (LittleFS.exists(fname)) {
                File f = LittleFS.open(fname, "r");
                _httpServer.streamFile(f, "text/csv");
                f.close();
                return;
            }
        }
        _httpServer.send(404, "text/plain", "Not found");
    });
}

void WifiManager::telnetTask(void* arg) {
    WifiManager* self = (WifiManager*)arg;
    WiFiServer& server = self->_telnetServer;
    server.begin();
    // use the member client array
    WiFiClient* clients = self->_clients;
    const int MAX_CLIENTS = WifiManager::MAX_TELNET_CLIENTS;

    for (;;) {
        if (server.hasClient()) {
            bool added = false;
            WiFiClient c = server.accept();
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (!clients[i] || !clients[i].connected()) {
                    if (clients[i]) clients[i].stop();
                    clients[i] = c;
                    if (clients[i]) clients[i].println("=== TELEMETRIA KART REMOTA CONECTADA ===");
                    added = true;
                    break;
                }
            }
            if (!added) {
                if (c) c.stop();
            }
        }

        // Simple echo of serial logs to telnet clients could be added here
        // (no-op here)
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void WifiManager::broadcast(const char* msg) {
    for (int i = 0; i < MAX_TELNET_CLIENTS; ++i) {
        if (_clients[i] && _clients[i].connected()) {
            _clients[i].print(msg);
        }
    }
}
