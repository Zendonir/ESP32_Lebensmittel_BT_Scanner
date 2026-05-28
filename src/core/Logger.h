#pragma once
#include <Arduino.h>
#include <freertos/portmacro.h>
#include <freertos/semphr.h>
#include <FS.h>

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

struct LogEntry {
    uint32_t ms;
    LogLevel level;
    char     module[16];
    char     message[120];
};

class Logger {
public:
    static void begin(uint32_t baud = 115200);
    static void log(LogLevel level, const char *module, const String &message);
    static void debug(const char *module, const String &message);
    static void info (const char *module, const String &message);
    static void warn (const char *module, const String &message);
    static void error(const char *module, const String &message);

    // Ring buffer for web UI log viewer
    static void getLogJson(String &out);
    static void clearLog();

    // SD card file logging — call after SD is mounted and time is valid
    static void enableSdLog(fs::FS *sdFs);
    static void rotateSdLogIfNeeded();  // call from loop() daily
    static void flushSd();              // call from loop() every ~10 s

private:
    static const char *levelName(LogLevel level);
    static void        appendToSdBuffer(const char *line, size_t len);
    static void        flushSdBuffer();
    static void        pruneOldLogs();

    static constexpr int RING_SIZE = 150;
    static LogEntry     *_ring;       // allocated from PSRAM in begin()
    static int           _ringHead;
    static int           _ringCount;
    static portMUX_TYPE  _mux;

    // SD logging
    static bool              _sdEnabled;
    static char              _sdLogPath[48];
    static SemaphoreHandle_t _sdMutex;
    static fs::FS           *_sdFs;

    // Write buffer – allocated from PSRAM in begin(), flushed every ~10 s
    static constexpr size_t  SD_BUF_SIZE = 4096;
    static char             *_sdBuf;  // allocated from PSRAM in begin()
    static size_t            _sdBufLen;
    static uint32_t          _sdLastFlush;
};
