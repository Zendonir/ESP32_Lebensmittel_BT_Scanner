#include "Logger.h"
#include <vector>
#include <algorithm>
#include <esp_heap_caps.h>

// _ring and _sdBuf are allocated from PSRAM in begin() so internal SRAM isn't consumed.
LogEntry          *Logger::_ring      = nullptr;
int                Logger::_ringHead  = 0;
int                Logger::_ringCount = 0;
portMUX_TYPE       Logger::_mux       = portMUX_INITIALIZER_UNLOCKED;
bool               Logger::_sdEnabled = false;
char               Logger::_sdLogPath[48] = {};
SemaphoreHandle_t  Logger::_sdMutex   = nullptr;
fs::FS            *Logger::_sdFs      = nullptr;
char              *Logger::_sdBuf     = nullptr;
size_t             Logger::_sdBufLen  = 0;
uint32_t           Logger::_sdLastFlush = 0;

static constexpr size_t SD_MAX_FILE_BYTES = 512UL * 1024;
static constexpr int    SD_KEEP_DAYS      = 7;
static const char*      SD_LOG_DIR        = "/scanner_log";

void Logger::begin(uint32_t baud) {
    Serial.begin(baud);
    Serial.setDebugOutput(true);
    uint32_t start = millis();
    while (!Serial && millis() - start < 1500) delay(10);

    // Allocate ring buffer and SD write buffer from PSRAM to keep internal SRAM free.
    if (!_ring)
        _ring = (LogEntry *)heap_caps_calloc(RING_SIZE, sizeof(LogEntry),
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!_sdBuf)
        _sdBuf = (char *)heap_caps_calloc(1, SD_BUF_SIZE,
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// ── SD logging ────────────────────────────────────────────────────────────────

void Logger::enableSdLog(fs::FS *sdFs) {
    if (!sdFs) return;
    _sdFs = sdFs;

    if (!_sdMutex) _sdMutex = xSemaphoreCreateMutex();
    if (!_sdMutex) return;

    if (!_sdFs->exists(SD_LOG_DIR)) _sdFs->mkdir(SD_LOG_DIR);

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year < 120) {
        snprintf(_sdLogPath, sizeof(_sdLogPath), "%s/boot_%lu.log",
                 SD_LOG_DIR, (unsigned long)millis() / 1000);
    } else {
        snprintf(_sdLogPath, sizeof(_sdLogPath), "%s/%04d-%02d-%02d.log",
                 SD_LOG_DIR, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    }

    pruneOldLogs();
    _sdEnabled  = true;
    _sdBufLen   = 0;
    _sdLastFlush = millis();

    // Boot separator
    char sep[80], tbuf[24] = "??:??:??";
    if (t.tm_year >= 120)
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &t);
    int len = snprintf(sep, sizeof(sep),
        "\n=== BOOT %s (up %lu s) ===\n", tbuf, (unsigned long)millis() / 1000);
    appendToSdBuffer(sep, (size_t)len);
    flushSdBuffer();  // flush boot separator immediately
}

void Logger::rotateSdLogIfNeeded() {
    if (!_sdEnabled || !_sdFs) return;
    if (xSemaphoreTake(_sdMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;

    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year >= 120) {
        char todayPath[48];
        snprintf(todayPath, sizeof(todayPath), "%s/%04d-%02d-%02d.log",
                 SD_LOG_DIR, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
        if (strcmp(todayPath, _sdLogPath) != 0) {
            strncpy(_sdLogPath, todayPath, sizeof(_sdLogPath) - 1);
            _sdLogPath[sizeof(_sdLogPath) - 1] = '\0';
            pruneOldLogs();
        }
    }
    xSemaphoreGive(_sdMutex);
}

// Append a formatted line to the in-RAM write buffer.
// Must be called with _sdMutex held (or before _sdEnabled is true).
void Logger::appendToSdBuffer(const char *line, size_t len) {
    if (!line || len == 0 || !_sdBuf) return;
    if (_sdBufLen + len + 1 >= SD_BUF_SIZE) {
        // Buffer full: flush first (recursive-safe: we only call flushSdBuffer
        // which doesn't call appendToSdBuffer)
        flushSdBuffer();
    }
    memcpy(_sdBuf + _sdBufLen, line, len);
    _sdBufLen += len;
}

// Write the accumulated buffer to SD in one operation.
void Logger::flushSdBuffer() {
    if (!_sdEnabled || !_sdFs || _sdBufLen == 0) return;
    File f = _sdFs->open(_sdLogPath, FILE_APPEND);
    if (f) {
        f.write((const uint8_t *)_sdBuf, _sdBufLen);
        f.close();
    }
    _sdBufLen    = 0;
    _sdLastFlush = millis();
}

// Public flush — called from App::loop() every ~10 s
void Logger::flushSd() {
    if (!_sdEnabled || !_sdFs || !_sdMutex) return;
    if (xSemaphoreTake(_sdMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
    flushSdBuffer();
    xSemaphoreGive(_sdMutex);
}

void Logger::pruneOldLogs() {
    if (!_sdFs) return;
    File dir = _sdFs->open(SD_LOG_DIR);
    if (!dir || !dir.isDirectory()) return;

    std::vector<String> files;
    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            String name = entry.name();
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (name.endsWith(".log")) files.push_back(name);
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();

    if ((int)files.size() <= SD_KEEP_DAYS) return;
    std::sort(files.begin(), files.end());
    int toDelete = (int)files.size() - SD_KEEP_DAYS;
    for (int i = 0; i < toDelete; i++) {
        String path = String(SD_LOG_DIR) + "/" + files[i];
        _sdFs->remove(path.c_str());
    }
}

// ── Core log function ─────────────────────────────────────────────────────────

void Logger::log(LogLevel level, const char *module, const String &message) {
    Serial.printf("[%10lu] [%-5s] [%s] %s\n",
                  millis(), levelName(level), module, message.c_str());
    Serial.flush();

    if (_ring) {
        portENTER_CRITICAL(&_mux);
        LogEntry &e = _ring[_ringHead];
        e.ms    = millis();
        e.level = level;
        strncpy(e.module,  module,          sizeof(e.module)  - 1);
        e.module[sizeof(e.module) - 1]  = '\0';
        strncpy(e.message, message.c_str(), sizeof(e.message) - 1);
        e.message[sizeof(e.message) - 1] = '\0';
        _ringHead = (_ringHead + 1) % RING_SIZE;
        if (_ringCount < RING_SIZE) _ringCount++;
        portEXIT_CRITICAL(&_mux);
    }

    // Write all levels INFO and above to SD via the write buffer.
    // WARN/ERROR flush immediately; INFO/DEBUG accumulate until flushSd() is called.
    if (_sdEnabled && level >= LogLevel::INFO && _sdMutex) {
        if (xSemaphoreTake(_sdMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            char line[180];
            time_t now = time(nullptr);
            struct tm t;
            localtime_r(&now, &t);
            char tbuf[16] = "??:??:??";
            if (t.tm_year >= 120)
                snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d",
                         t.tm_hour, t.tm_min, t.tm_sec);
            int len = snprintf(line, sizeof(line), "[%s] [%-5s] [%s] %s\n",
                               tbuf, levelName(level), module, message.c_str());
            if (len > 0) {
                appendToSdBuffer(line, (size_t)len);
                // Flush immediately for WARN/ERROR so they are never lost
                if (level >= LogLevel::WARN) flushSdBuffer();
            }
            xSemaphoreGive(_sdMutex);
        }
    }
}

void Logger::debug(const char *m, const String &msg) { log(LogLevel::DEBUG, m, msg); }
void Logger::info (const char *m, const String &msg) { log(LogLevel::INFO,  m, msg); }
void Logger::warn (const char *m, const String &msg) { log(LogLevel::WARN,  m, msg); }
void Logger::error(const char *m, const String &msg) { log(LogLevel::ERROR, m, msg); }

const char *Logger::levelName(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
    }
    return "INFO";
}

void Logger::getLogJson(String &out) {
    if (!_ring) { out = "[]"; return; }

    portENTER_CRITICAL(&_mux);
    int count = _ringCount;
    int head  = _ringHead;
    portEXIT_CRITICAL(&_mux);

    int start = (count < RING_SIZE) ? 0 : head;
    out = "[";
    for (int i = 0; i < count; i++) {
        int idx = (start + i) % RING_SIZE;
        const LogEntry &e = _ring[idx];
        if (i > 0) out += ',';
        out += "{\"time\":";
        out += e.ms;
        out += ",\"level\":\"";
        out += levelName(e.level);
        out += "\",\"module\":\"";
        String mod = e.module;
        mod.replace("\\", "\\\\"); mod.replace("\"", "\\\"");
        out += mod;
        out += "\",\"message\":\"";
        String msg = e.message;
        msg.replace("\\", "\\\\"); msg.replace("\"", "\\\"");
        out += msg;
        out += "\"}";
    }
    out += "]";
}

void Logger::clearLog() {
    portENTER_CRITICAL(&_mux);
    _ringHead  = 0;
    _ringCount = 0;
    portEXIT_CRITICAL(&_mux);
}
