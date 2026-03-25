#ifndef ERROR_LOG_MANAGER_H
#define ERROR_LOG_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include "EspNowProtocol.h"

#define ERROR_LOG_FILE "/helmet_error.log"
#define ERROR_LOG_MAX_LINE_LENGTH 200

class ErrorLogManager {
public:
    // Initialize the error log manager
    bool begin();

    // Log an error message to LittleFS
    // Automatically creates file if it doesn't exist, appends if it does
    void logError(const char* errorMsg);

    // Send all stored error logs to the display via ESP-NOW
    // Returns true if transmission was successful
    bool sendStoredLogsToDisplay();

    // Check if an error log file exists
    bool logFileExists();

    // Delete the error log file (called after display confirms receipt)
    bool deleteLogFile();

    // Get the number of lines in the error log
    uint16_t getLogLineCount();

private:
    // Helper to count lines in the log file
    uint16_t countLines();

    // Helper to send a single line to the display
    bool sendLogLine(uint16_t lineNumber, uint16_t totalLines, const String &lineContent);

    // Helper to send control message (start/end)
    bool sendControlMessage(uint8_t controlType, uint16_t count);
};

#endif
