#pragma once
#include <Arduino.h>

class NtfyNotifier {
public:
    // Send a notification. serverUrl e.g. "https://ntfy.sh", topic e.g. "myscanner123"
    // Returns true on HTTP 2xx.
    static bool send(const String &serverUrl, const String &topic,
                     const String &title, const String &message,
                     const String &priority = "default");
};
