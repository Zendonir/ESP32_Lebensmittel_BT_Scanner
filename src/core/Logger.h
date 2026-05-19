#pragma once
#include <Arduino.h>
#include <freertos/portmacro.h>

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

struct LogEntry {
    uint32_t ms;
    LogLevel level;
    char     module[16];
    char     message[100];
};

class Logger {
public:
    static void begin(uint32_t baud = 115200);
    static void log(LogLevel level, const char *module, const String &message);
    static void debug(const char *module, const String &message);
    static void info(const char *module, const String &message);
    static void warn(const char *module, const String &message);
    static void error(const char *module, const String &message);

    // Ring buffer for web UI log viewer
    static void getLogJson(String &out);
    static void clearLog();

private:
    static const char *levelName(LogLevel level);

    static constexpr int RING_SIZE = 80;
    static LogEntry      _ring[RING_SIZE];
    static int           _ringHead;
    static int           _ringCount;
    static portMUX_TYPE  _mux;
};
