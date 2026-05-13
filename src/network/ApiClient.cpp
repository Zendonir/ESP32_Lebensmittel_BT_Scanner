#include "ApiClient.h"
#include "../core/Logger.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

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
    // STRICT only follows same-protocol redirects; OpenFoodFacts HTTP→HTTPS
    // would need FORCE, but that just hits the same SSL-memory wall, so we
    // skip redirect following here and always use HTTPS directly.
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.useHTTP10(true);

    if (url.startsWith("https://")) {
        WiFiClientSecure client;
        client.setInsecure();
        // Default TLS buffers are 16 KB in + 16 KB out = ~32 KB on the heap.
        // ESP32-S3 running BLE+WiFi+LVGL rarely has that much contiguous DRAM.
        // 4096/512 covers the OpenFoodFacts JSON response (typically <3 KB)
        // and cuts SSL heap demand to ~6 KB.
        client.setBufferSizes(4096, 512);
        client.setTimeout(timeoutMs / 1000);
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

