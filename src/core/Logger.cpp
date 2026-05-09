#include "Logger.h"

void Logger::begin(uint32_t baud) {
    Serial.begin(baud);
    Serial.setDebugOutput(true);

    // ESP32-S3 boards often expose the log console via native USB CDC.
    // Give the host a short moment to attach, but do not block headless boots.
    uint32_t start = millis();
    while (!Serial && millis() - start < 1500) {
        delay(10);
    }
}

void Logger::log(LogLevel level, const char *module, const String &message) {
    Serial.printf("[%10lu] [%-5s] [%s] %s\n", millis(), levelName(level), module, message.c_str());
    Serial.flush();
}

void Logger::debug(const char *module, const String &message) { log(LogLevel::DEBUG, module, message); }
void Logger::info(const char *module, const String &message) { log(LogLevel::INFO, module, message); }
void Logger::warn(const char *module, const String &message) { log(LogLevel::WARN, module, message); }
void Logger::error(const char *module, const String &message) { log(LogLevel::ERROR, module, message); }

const char *Logger::levelName(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
    }
    return "INFO";
}
