#include "JsonStorage.h"
#include "../core/Logger.h"

JsonStorage::JsonStorage(LittleFSManager &fs) : filesystem(fs) {}

bool JsonStorage::loadDocument(const char *path, JsonDocument &doc, const char *fallbackJson) {
    String raw;
    if (!filesystem.readFile(path, raw)) raw = fallbackJson;
    DeserializationError error = deserializeJson(doc, raw);
    if (!error) return true;

    Logger::warn("JsonStorage", String("Recovering corrupt file: ") + path);
    doc.clear();
    deserializeJson(doc, fallbackJson);
    filesystem.writeFileAtomic(path, fallbackJson);
    return false;
}

bool JsonStorage::saveDocument(const char *path, JsonDocument &doc) {
    String raw;
    serializeJson(doc, raw);
    return filesystem.writeFileAtomic(path, raw);
}
