#pragma once
#include <Arduino.h>
#include <WiFiClient.h>
#include <vector>

// Lightweight MySQL client: mysql_native_password auth + COM_QUERY.
// Supports DDL/DML (execute) and SELECT result-set parsing (queryRows).
// Uses mbedTLS SHA1 (built into ESP32 IDF, no extra library needed).
class MySQLDirect {
public:
    bool connect(const String &ip, uint16_t port,
                 const String &user, const String &pass,
                 uint32_t timeoutMs = 6000);
    bool execute(const String &sql);
    // Run a SELECT and collect each row as a vector of column strings.
    // Returns false on connection/protocol error. Rows limited to maxRows.
    bool queryRows(const String &sql,
                   std::vector<std::vector<String>> &out,
                   size_t maxRows = 200);
    void close();
    const String& lastError() const { return _lastError; }

private:
    WiFiClient _client;
    String     _lastError;
    uint8_t    _seq = 0;
    uint32_t   _readTimeoutMs = 5000;

    bool readN(uint8_t *buf, size_t n);
    bool readPacket(uint8_t *buf, size_t &len, size_t maxLen);
    void sendPacket(const uint8_t *payload, size_t len);
    bool readOkOrErr();

    // Read a length-encoded integer from buf at pos; advance pos
    static uint64_t lenencInt(const uint8_t *buf, size_t bufLen, size_t &pos);

    static void sha1(const uint8_t *data, size_t len, uint8_t out[20]);
    static void nativePassword(const String &pass,
                               const uint8_t nonce[20], uint8_t out[20]);
};
