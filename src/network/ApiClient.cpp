#include "ApiClient.h"
#include "../core/Logger.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/platform.h>
#include <esp_heap_caps.h>

// mbedTLS custom allocator: routes SSL buffers (~40 KB) to PSRAM so internal
// SRAM is not exhausted when BLE is active simultaneously.
static void *ssl_calloc_psram(size_t n, size_t sz) {
    // Prefer PSRAM; fall back to default heap if PSRAM is full or unavailable.
    void *p = heap_caps_calloc(n, sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(n, sz, MALLOC_CAP_DEFAULT);
    return p;
}

namespace {
ApiResponse finishGet(HTTPClient &http) {
    ApiResponse response;
    response.status = http.GET();
    if (response.status > 0) response.body = http.getString();
    http.end();
    return response;
}
}

ApiResponse ApiClient::get(const String &url, uint32_t timeoutMs) {
    HTTPClient http;
    http.setTimeout(timeoutMs);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.useHTTP10(true);
    // Required by OpenFoodFacts – requests without a User-Agent are throttled.
    http.setUserAgent("ESP32-FoodScanner/1.0 (ESP32-S3; github.com/user/foodscanner)");

    if (url.startsWith("https://")) {
        // Redirect mbedTLS allocations to PSRAM for this connection.
        mbedtls_platform_set_calloc_free(ssl_calloc_psram, heap_caps_free);

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(timeoutMs / 1000);
        ApiResponse r;
        if (!http.begin(client, url)) {
            Logger::error("ApiClient", "HTTPS begin failed");
        } else {
            r = finishGet(http);
        }

        // Restore default allocator so other mbedTLS users (MQTT etc.) are unaffected.
        mbedtls_platform_set_calloc_free(calloc, free);
        return r;
    }

    WiFiClient client;
    if (!http.begin(client, url)) {
        Logger::error("ApiClient", "HTTP begin failed");
        return ApiResponse();
    }
    return finishGet(http);
}

