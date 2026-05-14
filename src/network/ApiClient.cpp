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
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    http.useHTTP10(true);
    // Required by OpenFoodFacts – requests without a User-Agent are throttled.
    http.setUserAgent("ESP32-FoodScanner/1.0 (ESP32-S3; github.com/user/foodscanner)");

    if (url.startsWith("https://")) {
        WiFiClientSecure client;
        client.setInsecure();
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

