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
    client.setTimeout(5);            // s – TLS-Read-Timeout
    HTTPClient http;
    http.setConnectTimeout(4000);    // ms – nicht ewig auf TCP-Connect warten
    http.setTimeout(5000);           // ms – Gesamt-Read-Timeout
    if (!http.begin(client, url)) return false;
    http.addHeader("Content-Type", "text/plain; charset=utf-8");
    http.addHeader("Title", title);
    http.addHeader("Priority", priority);
    http.addHeader("Tags", "package");
    int code = http.POST(message);
    http.end();
    return code >= 200 && code < 300;
}
