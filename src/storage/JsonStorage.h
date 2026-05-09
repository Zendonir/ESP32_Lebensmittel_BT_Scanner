#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "LittleFSManager.h"

class JsonStorage {
public:
    explicit JsonStorage(LittleFSManager &fs);
    bool loadDocument(const char *path, JsonDocument &doc, const char *fallbackJson = "[]");
    bool saveDocument(const char *path, JsonDocument &doc);

private:
    LittleFSManager &filesystem;
};
