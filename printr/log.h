// Leveled serial logging. Format strings are kept in flash (PSTR) so they
// don't eat RAM, and messages are timestamped with seconds since boot.
#pragma once

#include <Arduino.h>

enum LogLevel : uint8_t { LOG_ERROR = 1, LOG_WARN = 2, LOG_INFO = 3, LOG_DEBUG = 4 };

#ifndef PRINTR_LOG_LEVEL
#define PRINTR_LOG_LEVEL LOG_INFO
#endif

void logWrite(LogLevel level, PGM_P fmt, ...) __attribute__((format(printf, 2, 3)));

#define LOGE(fmt, ...) logWrite(LOG_ERROR, PSTR(fmt), ##__VA_ARGS__)
#define LOGW(fmt, ...) logWrite(LOG_WARN, PSTR(fmt), ##__VA_ARGS__)
#define LOGI(fmt, ...) logWrite(LOG_INFO, PSTR(fmt), ##__VA_ARGS__)
#define LOGD(fmt, ...) logWrite(LOG_DEBUG, PSTR(fmt), ##__VA_ARGS__)
