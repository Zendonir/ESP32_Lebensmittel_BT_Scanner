#include "ApiClient.h"
#include "../core/Logger.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

ApiResponse ApiClient::get(const String &url, uint32_t timeoutMs) {
    ApiResponse response;
    WiFiClientSecure client;
    // Arduino-ESP32 NetworkClientSecure attaches the ESP-IDF CRT bundle by default.
    HTTPClient http;
    http.setTimeout(timeoutMs);
    if (!http.begin(client, url)) {
        Logger::error("ApiClient", "HTTP begin failed");
        return response;
    }
    response.status = http.GET();
    if (response.status > 0) response.body = http.getString();
    http.end();
    return response;
}
