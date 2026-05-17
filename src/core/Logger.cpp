#include "Logger.h"

LogEntry     Logger::_ring[Logger::RING_SIZE];
int          Logger::_ringHead  = 0;
int          Logger::_ringCount = 0;
portMUX_TYPE Logger::_mux       = portMUX_INITIALIZER_UNLOCKED;

void Logger::begin(uint32_t baud) {
    Serial.begin(baud);
    Serial.setDebugOutput(true);
    uint32_t start = millis();
    while (!Serial && millis() - start < 1500) delay(10);
}

void Logger::log(LogLevel level, const char *module, const String &message) {
    Serial.printf("[%10lu] [%-5s] [%s] %s\n", millis(), levelName(level), module, message.c_str());
    Serial.flush();

    // Append to ring buffer under spinlock (very short critical section)
    portENTER_CRITICAL(&_mux);
    LogEntry &e = _ring[_ringHead];
    e.ms    = millis();
    e.level = level;
    strncpy(e.module,  module,            sizeof(e.module)  - 1);
    e.module[sizeof(e.module) - 1]  = '\0';
    strncpy(e.message, message.c_str(),   sizeof(e.message) - 1);
    e.message[sizeof(e.message) - 1] = '\0';
    _ringHead = (_ringHead + 1) % RING_SIZE;
    if (_ringCount < RING_SIZE) _ringCount++;
    portEXIT_CRITICAL(&_mux);
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
    // Snapshot indices atomically, then read entries lock-free
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
