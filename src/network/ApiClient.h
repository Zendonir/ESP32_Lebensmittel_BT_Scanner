#pragma once

#include <Arduino.h>

struct ApiResponse {
    int status = 0;
    String body;
};

class ApiClient {
public:
    ApiResponse get(const String &url, uint32_t timeoutMs = 8000);
};
