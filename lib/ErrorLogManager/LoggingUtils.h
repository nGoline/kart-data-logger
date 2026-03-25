/**
 * @file LoggingUtils.h
 * @brief Convenience macros for error logging throughout the application.
 * 
 * This header provides easy-to-use macros for logging errors to LittleFS.
 * The ErrorLogManager must be initialized before using these macros.
 * 
 * Usage:
 *   LOG_ERROR("GPS initialization failed");
 *   LOG_ERROR_FORMATTED("Battery level: %d%%", battLevel);
 */

#ifndef LOGGING_UTILS_H
#define LOGGING_UTILS_H

#include "ErrorLogManager.h"

// Forward declaration - must be defined in main_logger.cpp
extern ErrorLogManager errorLogger;

/**
 * Log an error message to the helmet error log.
 * Example: LOG_ERROR("Component initialization failed");
 */
#define LOG_ERROR(msg) \
    do { \
        errorLogger.logError(msg); \
        log_e("%s", msg); \
    } while(0)

/**
 * Log a formatted error message to the helmet error log.
 * Example: LOG_ERROR_FORMATTED("Module %s failed: %d", name, code);
 */
#define LOG_ERROR_FORMATTED(fmt, ...) \
    do { \
        static char _errBuf[256]; \
        snprintf(_errBuf, sizeof(_errBuf), fmt, ##__VA_ARGS__); \
        errorLogger.logError(_errBuf); \
        log_e("%s", _errBuf); \
    } while(0)

#endif
