#include "NtfyNotifier.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

bool NtfyNotifier::send(const String &serverUrl, const String &topic,
                        const String &title, const String &message,
                        const String &priority) {
    if (topic.isEmpty()) return false;
    String url = serverUrl;
    if (!url.endsWith("/")) url += "/";
    url += topic;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, url)) return false;
    http.addHeader("Content-Type", "text/plain; charset=utf-8");
    http.addHeader("Title", title);
    http.addHeader("Priority", priority);
    http.addHeader("Tags", "package");
    int code = http.POST(message);
    http.end();
    return code >= 200 && code < 300;
}
