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
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.useHTTP10(true);

    if (url.startsWith("https://")) {
        WiFiClientSecure client;
        // Product lookups must work on devices without an installed CA bundle.
        // Keep the request HTTPS, but skip chain validation instead of failing every
        // Open Food Facts request with start_ssl_client/connect failed.
        client.setInsecure();
        client.setTimeout(timeoutMs / 1000);
        client.setBufferSizes(512, 512);
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
