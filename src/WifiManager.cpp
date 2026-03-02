#include "WifiManager.h"
#include "LogManager.h"

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

    // Start a small HTTP handler task to service incoming requests
    xTaskCreate([](void* arg){
        WifiManager* self = (WifiManager*)arg;
        for (;;) {
            self->_httpServer.handleClient();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }, "HTTP_Task", 4096, this, 1, NULL);

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
                // normalize name to start with '/'
                if (!name.startsWith("/")) name = "/" + name;
                if (name.startsWith("/telemetry_") && name.endsWith(".csv")) {
                    // link to /logs/<path-with-leading-slash> so NotFound handler can strip '/logs' and open it directly
                    html += "<li><a href=\"/logs" + name + "\">" + name + "</a> (" + String(size) + " bytes) ";
                    // delete link
                    html += "<a href=\"/logs/delete?f=" + name + "\" style=\"color:red;margin-left:8px\">Delete</a>";
                    html += "</li>";
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

    // Delete a log file safely: /logs/delete?f=/telemetry_xxx.csv
    _httpServer.on("/logs/delete", HTTP_GET, [&]() {
        String fname = _httpServer.arg("f");
        if (!fname.startsWith("/")) fname = "/" + fname;

        // Safety checks: only allow telemetry CSVs and no directory traversal
        if (fname.indexOf("..") != -1 || !fname.startsWith("/telemetry_") || !fname.endsWith(".csv")) {
            _httpServer.send(400, "text/plain", "Invalid file");
            return;
        }

        // Try using the LogManager to safely remove the file (it will close active log if needed)
        extern LogManager logManager;
        bool ok = false;
        if (LittleFS.exists(fname)) {
            ok = logManager.removeLog(fname.c_str());
        }

        if (!ok) {
            // Provide diagnostic info
            String msg = "Failed to delete ";
            msg += fname;
            msg += "\n";
            if (LittleFS.exists(fname)) msg += "File still exists\n";
            else msg += "File removed or not present\n";
            _httpServer.send(500, "text/plain", msg);
            return;
        }

        // Redirect back to listing
        _httpServer.sendHeader("Location", "/logs");
        _httpServer.send(303, "text/plain", "");
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
