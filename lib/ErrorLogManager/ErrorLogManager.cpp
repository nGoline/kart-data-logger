#include "ErrorLogManager.h"
#include "EspNowManager.h"
#include <esp_now.h>

bool ErrorLogManager::begin() {
    // LittleFS has already been mounted by the caller. A root-path exists()
    // check is not a reliable readiness probe on ESP32 LittleFS.
    log_i("ErrorLogManager initialized.");
    return true;
}

void ErrorLogManager::logError(const char* errorMsg) {
    if (errorMsg == nullptr || strlen(errorMsg) == 0) {
        return;
    }

    File file = LittleFS.open(ERROR_LOG_FILE, FILE_APPEND);
    if (!file) {
        // If the file does not exist yet, create it.
        file = LittleFS.open(ERROR_LOG_FILE, FILE_WRITE);
    }

    if (!file) {
        log_e("Failed to open error log file for writing!");
        return;
    }

    // Add timestamp if available
    uint32_t uptime = millis();
    file.printf("[%u] %s\n", uptime, errorMsg);
    file.close();

    log_d("Error logged: %s", errorMsg);
}

bool ErrorLogManager::logFileExists() {
    return LittleFS.exists(ERROR_LOG_FILE);
}

uint16_t ErrorLogManager::countLines() {
    if (!logFileExists()) {
        return 0;
    }

    File file = LittleFS.open(ERROR_LOG_FILE, "r");
    if (!file) {
        log_e("Failed to open error log file for reading!");
        return 0;
    }

    uint16_t lineCount = 0;
    int lastChar = -1;
    while (file.available()) {
        lastChar = file.read();
        if (lastChar == '\n') {
            lineCount++;
        }
    }

    if (lastChar != -1 && lastChar != '\n') {
        lineCount++;
    }

    file.close();
    return lineCount;
}

uint16_t ErrorLogManager::getLogLineCount() {
    return countLines();
}

bool ErrorLogManager::sendControlMessage(uint8_t controlType, uint16_t count) {
    ErrorLogControlMsg msg;
    msg.type = controlType;
    msg.totalLines = count;

    uint8_t bcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t res = esp_now_send(bcastAddr, (uint8_t*)&msg, sizeof(msg));
    
    if (res != ESP_OK) {
        log_w("Failed to send error log control message! Error: %d", res);
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay between messages
    return true;
}

bool ErrorLogManager::sendLogLine(uint16_t lineNumber, uint16_t totalLines, const String &lineContent) {
    ErrorLogLineMsg msg;
    msg.type = MSG_ERROR_LOG_LINE;
    msg.lineNumber = lineNumber;
    msg.totalLines = totalLines;
    
    // Ensure null termination
    strncpy(msg.lineData, lineContent.c_str(), ERROR_LOG_MAX_LINE_LENGTH - 1);
    msg.lineData[ERROR_LOG_MAX_LINE_LENGTH - 1] = '\0';

    uint8_t bcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_err_t res = esp_now_send(bcastAddr, (uint8_t*)&msg, sizeof(msg));
    
    if (res != ESP_OK) {
        log_w("Failed to send error log line %d! Error: %d", lineNumber, res);
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay between messages
    return true;
}

bool ErrorLogManager::sendStoredLogsToDisplay() {
#if !defined(IS_LOGGER)
    // This flow is logger-only. Keep a safe stub for non-logger targets.
    log_w("sendStoredLogsToDisplay() called on non-logger build.");
    return false;
#else
    if (!logFileExists()) {
        log_i("No error log file to send.");
        return true;
    }

    File file = LittleFS.open(ERROR_LOG_FILE, "r");
    if (!file) {
        log_e("Failed to open error log file for transmission!");
        return false;
    }

    // Count total lines first
    uint16_t totalLines = countLines();
    EspNowManager::resetErrorLogAckState();
    
    // Send START message
    if (!sendControlMessage(MSG_ERROR_LOG_START, totalLines)) {
        file.close();
        return false;
    }

    log_i("Sending %d error log lines to display...", totalLines);

    // Read and send each line
    uint16_t lineNumber = 0;
    String currentLine;
    
    while (file.available()) {
        int c = file.read();
        
        if (c == '\n' || c == -1) {
            // End of line or end of file
            if (currentLine.length() > 0 || c != -1) {
                if (!sendLogLine(lineNumber, totalLines, currentLine)) {
                    log_e("Failed to send line %d", lineNumber);
                    file.close();
                    return false;
                }
                
                lineNumber++;
                currentLine = "";
                
                // Yield to other tasks periodically
                if (lineNumber % 10 == 0) {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
            
            if (c == -1) break; // EOF
        } else if (c > 0) {
            currentLine += (char)c;
        }
    }

    file.close();

    // Send END message
    if (!sendControlMessage(MSG_ERROR_LOG_END, totalLines)) {
        return false;
    }

    log_i("Error log transmission complete. Waiting for acknowledgment...");

    uint16_t ackLines = 0;
    const uint32_t ACK_TIMEOUT_MS = 5000;
    bool gotExpectedAck = EspNowManager::waitForErrorLogAck(totalLines, ACK_TIMEOUT_MS, ackLines);
    if (!gotExpectedAck) {
        log_w("ACK timeout/mismatch. Expected %u lines, got %u.", totalLines, ackLines);
        return false;
    }

    log_i("Display ACK confirmed %u/%u lines written.", ackLines, totalLines);
    return true;
#endif
}

bool ErrorLogManager::deleteLogFile() {
    if (!logFileExists()) {
        return true; // Already gone
    }

    if (LittleFS.remove(ERROR_LOG_FILE)) {
        log_i("Error log file deleted successfully.");
        return true;
    } else {
        log_e("Failed to delete error log file!");
        return false;
    }
}
