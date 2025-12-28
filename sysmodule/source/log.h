#pragma once

#include <stdbool.h>

void initLog(bool file, bool udp);
void exitLog();
void logImpl(const char* type, const char* file, int line, const char* format, ...);


#define LOG_INFO(format, ...) \
    logImpl("INFO", __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_WARNING(format, ...) \
    logImpl("WARNING", __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_ERROR(format, ...)                          \
    logImpl("ERROR", __FILE__, __LINE__, format, ##__VA_ARGS__)


#define LOG_FILE_PATH   "nextprof_sys.log"
#define LOG_SERVER_IP   "192.168.178.103"
#define LOG_SERVER_PORT 8008
