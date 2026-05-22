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

#ifdef IS_LOGGER

#include "ErrorLogManager.h"

// Defined in main_logger.cpp
extern ErrorLogManager errorLogger;

#define LOG_ERROR(msg) \
    do { \
        errorLogger.logError(msg); \
        log_e("%s", msg); \
    } while(0)

#define LOG_ERROR_FORMATTED(fmt, ...) \
    do { \
        static char _errBuf[256]; \
        snprintf(_errBuf, sizeof(_errBuf), fmt, ##__VA_ARGS__); \
        errorLogger.logError(_errBuf); \
        log_e("%s", _errBuf); \
    } while(0)

#else // IS_DISPLAY or any other non-logger build: serial only

#define LOG_ERROR(msg)                  log_e("%s", msg)
#define LOG_ERROR_FORMATTED(fmt, ...)   log_e(fmt, ##__VA_ARGS__)

#endif

#endif
