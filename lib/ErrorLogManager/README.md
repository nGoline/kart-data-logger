# Error Logging System

## Overview

The Error Logging System provides a reliable way to capture, store, and transmit error messages from the helmet logger to the display unit. Errors are logged to LittleFS in a file called `/helmet_error.log`. On startup, if a log file exists, it's automatically transmitted to the display via ESP-NOW, which can then record it to SD card for later analysis.

## Features

- **Persistent Error Storage**: Errors are written to `/helmet_error.log` with timestamps
- **Automatic Append**: If the log file exists, new errors are appended; if not, a new file is created
- **Line-by-Line Transmission**: Error logs are sent to the display in manageable chunks to avoid ESP-NOW payload limits
- **Startup Transmission**: On boot, any existing error log is automatically transmitted to the display
- **File Cleanup**: After successful transmission, the log file is deleted from LittleFS

## Architecture

### Message Types (EspNowProtocol.h)

```cpp
MSG_ERROR_LOG_START   = 0x30  // Signals start of log transmission
MSG_ERROR_LOG_LINE    = 0x31  // Single line of error log
MSG_ERROR_LOG_END     = 0x32  // Signals end of transmission
MSG_ERROR_LOG_ACK     = 0x33  // Display acknowledgment (for future use)
```

### Log Entry Format

Each line in the log file has the format:
```
[uptime_ms] error_message
```

Example:
```
[12345] GPS initialization failed
[45678] IMU sensor not responding
[78901] Battery critically low
```

## Usage

### Basic Error Logging

```cpp
#include "LoggingUtils.h"

// Simple error message
LOG_ERROR("Device initialization failed");

// Formatted error message
LOG_ERROR_FORMATTED("Sensor error code: 0x%02X", errorCode);
```

### Direct Access

For more control, you can use the ErrorLogManager directly:

```cpp
#include "ErrorLogManager.h"

extern ErrorLogManager errorLogger;

// Log an error
errorLogger.logError("Critical temperature reached");

// Check if log exists
if (errorLogger.logFileExists()) {
    uint16_t lineCount = errorLogger.getLogLineCount();
    log_i("Error log has %d lines", lineCount);
}

// Manually transmit logs (called automatically on startup)
errorLogger.sendStoredLogsToDisplay();

// Delete log file
errorLogger.deleteLogFile();
```

## Integration

The ErrorLogManager is initialized in `src/main_logger.cpp` during the setup phase:

1. **LittleFS Initialization**: Required before ErrorLogManager can function
2. **Startup Sequence**: After radio (ESP-NOW) is initialized, stored logs are automatically transmitted
3. **File Transmission**: Logs are sent line-by-line to avoid ESP-NOW payload limits (ESP-NOW max payload ~250 bytes)

### Initialization Order

```
Boot
 ↓
LittleFS.begin()
 ↓
ErrorLogManager.begin()
 ↓
Audio Engine Start
 ↓
Battery Check
 ↓
Radio (ESP-NOW) Init
 ↓
[If log exists] Send logs to display
 ↓
GPS Init
 ↓
Telemetry Task Start
```

## Future Enhancements

### TODO: ACK Handling
Currently, the system waits a fixed time before deleting the log file. A more robust approach would:

1. Implement `MSG_ERROR_LOG_ACK` handling to detect when the display has successfully recorded the log
2. Only delete the log file after receiving the acknowledgment
3. Implement retry logic if acknowledgment is not received

### TODO: Display-Side Implementation
The display needs to:

1. Add handlers for `MSG_ERROR_LOG_START`, `MSG_ERROR_LOG_LINE`, and `MSG_ERROR_LOG_END`
2. Buffer the incoming lines
3. Write them to `/helmet_error.log` on the SD card
4. Send `MSG_ERROR_LOG_ACK` back to the logger when complete

Example display-side structure:
```cpp
// In main_display.cpp
case MSG_ERROR_LOG_START:
    // Reset buffer, prepare to receive lines
    break;

case MSG_ERROR_LOG_LINE:
    // Append line to buffer
    break;

case MSG_ERROR_LOG_END:
    // Write complete log to SD card
    // Send acknowledgment back to logger
    errorLogManager.sendAck(totalLinesReceived);
    break;
```

## Error Log File Management

### Automatic Deletion
The error log file is automatically deleted after successful transmission to the display. This prevents the LittleFS from filling up.

### Manual Management

```cpp
// Check file existence
bool exists = errorLogger.logFileExists();

// Get line count without sending
uint16_t lines = errorLogger.getLogLineCount();

// Force deletion
errorLogger.deleteLogFile();
```

## Performance Considerations

- **Message Rate**: ~10ms delay between each message to avoid overwhelming the display
- **Line Length**: Maximum 200 characters per line (see `ErrorLogLineMsg` structure)
- **File Size**: Limited by LittleFS available space; typical logs should be well under 1MB

## Debugging

To view the error log on the logger:

```cpp
// In a debug task or loop
if (errorLogger.logFileExists()) {
    File file = LittleFS.open("/helmet_error.log", "r");
    while (file.available()) {
        Serial.write(file.read());
    }
    file.close();
}
```

## Examples

### Logging GPS Errors

```cpp
#include "LoggingUtils.h"

if (!gps.begin()) {
    LOG_ERROR("GPS initialization failed");
    LOG_ERROR_FORMATTED("GPS error code: %d", gps.getLastError());
}
```

### Logging IMU Errors

```cpp
if (!imu.readAccel()) {
    LOG_ERROR_FORMATTED("IMU read failed, status: 0x%02X", imu.getStatus());
}
```

### Logging Audio Errors

```cpp
if (!audio.begin(LittleFS, volume)) {
    LOG_ERROR("Audio engine initialization failed");
}
```

## Troubleshooting

**Q: Error log file grows too large**
- Implement periodic log rotation or size limits
- Consider compressing or archiving older entries

**Q: Display not receiving logs**
- Verify ESP-NOW is properly initialized
- Check that display-side handlers are implemented
- Verify broadcast address (0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF)

**Q: File not deleted after transmission**
- Currently implemented with a fixed delay; implement proper ACK handling
- Check LittleFS permissions and available space

## Related Files

- `lib/Shared/EspNowProtocol.h` - Message definitions
- `lib/ErrorLogManager/ErrorLogManager.h/.cpp` - Core implementation
- `lib/ErrorLogManager/LoggingUtils.h` - Helper macros
- `src/main_logger.cpp` - Initialization and startup integration
