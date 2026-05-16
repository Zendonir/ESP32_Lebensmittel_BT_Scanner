#include "MySQLDirect.h"
#include <mbedtls/sha1.h>
#include <string.h>

// ---------- SHA1 + native-password -----------------------------------------

void MySQLDirect::sha1(const uint8_t *data, size_t len, uint8_t out[20]) {
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts_ret(&ctx);
    mbedtls_sha1_update_ret(&ctx, data, len);
    mbedtls_sha1_finish_ret(&ctx, out);
    mbedtls_sha1_free(&ctx);
}

// MySQL native password = SHA1(pass) XOR SHA1(nonce || SHA1(SHA1(pass)))
void MySQLDirect::nativePassword(const String &pass,
                                  const uint8_t nonce[20], uint8_t out[20]) {
    if (pass.isEmpty()) { memset(out, 0, 20); return; }
    uint8_t h1[20], h12[20], h2[20], concat[40];
    sha1((const uint8_t*)pass.c_str(), pass.length(), h1);
    sha1(h1, 20, h12);
    memcpy(concat, nonce, 20);
    memcpy(concat + 20, h12, 20);
    sha1(concat, 40, h2);
    for (int i = 0; i < 20; i++) out[i] = h1[i] ^ h2[i];
}

// ---------- packet I/O -------------------------------------------------------

bool MySQLDirect::readN(uint8_t *buf, size_t n) {
    size_t got = 0;
    uint32_t t0 = millis();
    while (got < n) {
        if (millis() - t0 > 5000) { _lastError = "read timeout"; return false; }
        int a = _client.available();
        if (a > 0) {
            size_t chunk = (size_t)a < (n - got) ? (size_t)a : (n - got);
            got += _client.readBytes(buf + got, chunk);
        } else {
            delay(1);
        }
    }
    return true;
}

bool MySQLDirect::readPacket(uint8_t *buf, size_t &len, size_t maxLen) {
    uint8_t hdr[4];
    if (!readN(hdr, 4)) return false;
    uint32_t pktLen = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16);
    _seq = hdr[3] + 1;
    if (pktLen > maxLen) { _lastError = "packet too large"; return false; }
    if (!readN(buf, pktLen)) return false;
    len = pktLen;
    return true;
}

void MySQLDirect::sendPacket(const uint8_t *payload, size_t len) {
    uint8_t hdr[4] = {
        (uint8_t)(len & 0xFF),
        (uint8_t)((len >> 8) & 0xFF),
        (uint8_t)((len >> 16) & 0xFF),
        _seq++
    };
    _client.write(hdr, 4);
    _client.write(payload, len);
}

bool MySQLDirect::readOkOrErr() {
    uint8_t buf[256]; size_t len = 0;
    if (!readPacket(buf, len, sizeof(buf))) return false;
    if (len == 0) { _lastError = "empty packet"; return false; }
    if (buf[0] == 0xFF) {
        uint16_t code = (len >= 3) ? ((uint16_t)buf[1] | ((uint16_t)buf[2] << 8)) : 0;
        String msg;
        size_t start = (len > 3 && buf[3] == '#') ? 9 : 3;
        for (size_t i = start; i < len; i++) msg += (char)buf[i];
        _lastError = "MySQL " + String(code) + ": " + msg;
        return false;
    }
    // 0x00=OK, 0xFE=EOF — anything else for DDL/DML is also treated as success
    return true;
}

// ---------- connect ----------------------------------------------------------

bool MySQLDirect::connect(const String &ip, uint16_t port,
                           const String &user, const String &pass,
                           uint32_t timeoutMs) {
    _seq = 0; _lastError = "";
    IPAddress serverIP;
    if (!serverIP.fromString(ip)) { _lastError = "Invalid IP: " + ip; return false; }
    _client.setTimeout(timeoutMs / 1000);
    if (!_client.connect(serverIP, port)) {
        _lastError = "TCP connect failed to " + ip + ":" + port;
        return false;
    }

    // Read Initial Handshake packet
    uint8_t buf[320]; size_t len = 0;
    if (!readPacket(buf, len, sizeof(buf))) return false;
    if (buf[0] != 0x0A) {
        _lastError = "Unknown protocol version 0x" + String(buf[0], HEX);
        _client.stop(); return false;
    }

    // Skip null-terminated server version string
    size_t pos = 1;
    while (pos < len && buf[pos]) pos++;
    pos++;       // skip null
    pos += 4;    // connection id

    // auth-plugin-data part 1 (8 bytes) + 1 filler byte
    uint8_t nonce[20] = {};
    memcpy(nonce, buf + pos, 8); pos += 8; pos++;

    // capability flags low (2), charset (1), status (2), capability flags high (2)
    pos += 2; pos += 1; pos += 2; pos += 2;

    // auth-plugin-data length (1 byte), reserved (10 bytes)
    uint8_t authLen = (pos < len) ? buf[pos] : 0; pos += 1; pos += 10;

    // auth-plugin-data part 2: max(13, authLen-8) bytes including trailing null
    size_t part2Len = (authLen > 8) ? (size_t)(authLen - 8) : 13;
    if (part2Len > 13) part2Len = 13;
    if (pos + part2Len <= len)
        memcpy(nonce + 8, buf + pos, part2Len - 1);  // exclude trailing null

    // Compute auth response
    uint8_t authResp[20] = {};
    bool hasPass = !pass.isEmpty();
    if (hasPass) nativePassword(pass, nonce, authResp);

    // Build HandshakeResponse41
    constexpr uint32_t CAPS =
        0x00000001u |   // CLIENT_LONG_PASSWORD
        0x00000200u |   // CLIENT_PROTOCOL_41
        0x00008000u |   // CLIENT_SECURE_CONNECTION
        0x00080000u;    // CLIENT_PLUGIN_AUTH

    uint8_t pkt[256]; size_t ppos = 0;

    pkt[ppos++] = (uint8_t)(CAPS);       pkt[ppos++] = (uint8_t)(CAPS >> 8);
    pkt[ppos++] = (uint8_t)(CAPS >> 16); pkt[ppos++] = (uint8_t)(CAPS >> 24);
    // max packet size = 16 MB
    pkt[ppos++] = 0xFF; pkt[ppos++] = 0xFF; pkt[ppos++] = 0xFF; pkt[ppos++] = 0x00;
    pkt[ppos++] = 33;   // charset: utf8
    memset(pkt + ppos, 0, 23); ppos += 23;   // reserved

    // username + null
    size_t uLen = user.length();
    if (uLen > 63) uLen = 63;
    memcpy(pkt + ppos, user.c_str(), uLen); ppos += uLen;
    pkt[ppos++] = 0;

    // auth response (length-prefixed)
    if (hasPass) {
        pkt[ppos++] = 20;
        memcpy(pkt + ppos, authResp, 20); ppos += 20;
    } else {
        pkt[ppos++] = 0;
    }

    // plugin name + null
    const char *plugin = "mysql_native_password";
    size_t pnLen = strlen(plugin);
    memcpy(pkt + ppos, plugin, pnLen + 1); ppos += pnLen + 1;

    sendPacket(pkt, ppos);

    // Read auth result
    if (!readPacket(buf, len, sizeof(buf))) return false;
    if (buf[0] == 0x00) return true;   // OK
    if (buf[0] == 0xFF) {
        uint16_t code = (len >= 3) ? ((uint16_t)buf[1] | ((uint16_t)buf[2] << 8)) : 0;
        String msg;
        size_t start = (len > 3 && buf[3] == '#') ? 9 : 3;
        for (size_t i = start; i < len; i++) msg += (char)buf[i];
        _lastError = "Auth error " + String(code) + ": " + msg;
        _client.stop(); return false;
    }
    // 0x01 = caching_sha2 fast auth path, 0xFE = auth switch request
    _lastError = "Unsupported auth method (0x" + String(buf[0], HEX) +
                 ") – user must use mysql_native_password";
    _client.stop(); return false;
}

// ---------- execute ----------------------------------------------------------

bool MySQLDirect::execute(const String &sql) {
    _lastError = ""; _seq = 0;
    size_t sLen = sql.length();
    uint8_t *pkt = new uint8_t[1 + sLen];
    pkt[0] = 0x03;   // COM_QUERY
    memcpy(pkt + 1, sql.c_str(), sLen);
    sendPacket(pkt, 1 + sLen);
    delete[] pkt;
    return readOkOrErr();
}

// ---------- close ------------------------------------------------------------

void MySQLDirect::close() {
    if (_client.connected()) {
        uint8_t quit[1] = {0x01}; _seq = 0;
        sendPacket(quit, 1);
    }
    _client.stop();
}
