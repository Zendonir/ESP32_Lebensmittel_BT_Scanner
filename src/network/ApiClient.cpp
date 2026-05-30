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

// Set the mbedTLS allocator to PSRAM ONCE, process-wide. heap_caps_free works
// for both PSRAM and internal allocations, so all TLS users (this client, ntfy,
// OTA) can safely share it. Swapping it per-request (set→restore) was a race:
// a concurrent TLS session on another core could be torn down with the wrong
// free() → heap corruption.
static void ensureSslAllocPsram() {
    static bool done = false;
    if (done) return;
    mbedtls_platform_set_calloc_free(ssl_calloc_psram, heap_caps_free);
    done = true;
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
    http.setConnectTimeout(timeoutMs);   // bound the TCP connect phase too
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);
    http.setUserAgent("ESP32-Lebensmittel-Scanner/1.0 (ESP32-S3; github.com/Zendonir/ESP32_Lebensmittel_BT_Scanner)");

    if (url.startsWith("https://")) {
        ensureSslAllocPsram();   // one-time, process-wide (no per-request swap)

        WiFiClientSecure client;
        client.setInsecure();
        // setTimeout() is in seconds here; guarantee at least 1 s so sub-second
        // timeoutMs values don't round down to 0 (= block forever).
        client.setTimeout(timeoutMs < 1000 ? 1 : timeoutMs / 1000);
        if (!http.begin(client, url)) {
            Logger::error("ApiClient", "HTTPS begin failed");
            return ApiResponse();
        }
        return finishGet(http);
    }

    WiFiClient client;
    if (!http.begin(client, url)) {
        Logger::error("ApiClient", "HTTP begin failed");
        return ApiResponse();
    }
    return finishGet(http);
}

