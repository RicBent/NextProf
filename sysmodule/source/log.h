#pragma once

#include <stdbool.h>

void initLog();
void exitLog();
void logImpl(const char* type, const char* file, int line, const char* format, ...);


#ifdef _DEBUG
    #define LOG_TRACE(format, ...) \
        logImpl("TRACE", __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
    #define LOG_TRACE(format, ...)
#endif

#define LOG_INFO(format, ...) \
    logImpl("INFO", __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_WARNING(format, ...) \
    logImpl("WARNING", __FILE__, __LINE__, format, ##__VA_ARGS__)

#define LOG_ERROR(format, ...)                          \
    logImpl("ERROR", __FILE__, __LINE__, format, ##__VA_ARGS__)
