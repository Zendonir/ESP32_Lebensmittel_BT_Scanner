#pragma once

#include <Arduino.h>

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static void begin(uint32_t baud = 115200);
    static void log(LogLevel level, const char *module, const String &message);
    static void debug(const char *module, const String &message);
    static void info(const char *module, const String &message);
    static void warn(const char *module, const String &message);
    static void error(const char *module, const String &message);

private:
    static const char *levelName(LogLevel level);
};
