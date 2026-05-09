#include "OpenFoodFacts.h"
#include "../core/Logger.h"
#include <ArduinoJson.h>

namespace {
constexpr const char *OFF_CACHE_PATH = "/off_cache.json";
constexpr size_t OFF_CACHE_MAX_ITEMS = 50;

void productFromJson(const String &barcode, JsonObject obj, ProductInfo &product) {
    product.barcode = obj["barcode"] | barcode;
    product.name = obj["name"] | "";
    product.brand = obj["brand"] | "";
    product.nutriscore = obj["nutriscore"] | "";
    product.labels.clear();
    for (JsonVariant label : obj["labels"].as<JsonArray>()) {
        product.labels.push_back(label.as<String>());
    }
}

void productToJson(JsonObject obj, const ProductInfo &product) {
    obj["barcode"] = product.barcode;
    obj["name"] = product.name;
    obj["brand"] = product.brand;
    obj["nutriscore"] = product.nutriscore;
    JsonArray labels = obj["labels"].to<JsonArray>();
    for (const String &label : product.labels) labels.add(label);
    obj["cachedAt"] = millis();
}
}

OpenFoodFacts::OpenFoodFacts(ApiClient &client, LittleFSManager *cacheFs) : api(client), filesystem(cacheFs) {}

bool OpenFoodFacts::fetchProduct(const String &barcode, ProductInfo &product) {
    if (loadCachedProduct(barcode, product)) {
        Logger::info("OpenFoodFacts", String("Cache hit for ") + barcode);
        return true;
    }

    String path = "world.openfoodfacts.org/api/v3/product/" + barcode + ".json";
    ApiResponse response = api.get("https://" + path);
    if (response.status <= 0) {
        Logger::warn("OpenFoodFacts", "HTTPS failed, retrying Open Food Facts over HTTP");
        response = api.get("http://" + path);
    }
    if (response.status != 200) {
        Logger::warn("OpenFoodFacts", String("HTTP status ") + response.status);
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, response.body)) return false;
    JsonObject p = doc["product"];
    product.barcode = barcode;
    product.name = p["product_name"] | "";
    product.brand = p["brands"] | "";
    product.nutriscore = p["nutriscore_grade"] | "";
    product.labels.clear();
    for (JsonVariant label : p["labels_tags"].as<JsonArray>()) {
        product.labels.push_back(label.as<String>());
    }

    bool ok = !product.name.isEmpty();
    if (ok) cacheProduct(product);
    return ok;
}

bool OpenFoodFacts::loadCachedProduct(const String &barcode, ProductInfo &product) {
    if (filesystem == nullptr) return false;

    String raw;
    if (!filesystem->readFile(OFF_CACHE_PATH, raw)) return false;

    JsonDocument doc;
    if (deserializeJson(doc, raw) || !doc.is<JsonArray>()) return false;

    for (JsonObject obj : doc.as<JsonArray>()) {
        if (String(obj["barcode"] | "") == barcode) {
            productFromJson(barcode, obj, product);
            return !product.name.isEmpty();
        }
    }
    return false;
}

void OpenFoodFacts::cacheProduct(const ProductInfo &product) {
    if (filesystem == nullptr || product.barcode.isEmpty()) return;

    String raw;
    if (!filesystem->readFile(OFF_CACHE_PATH, raw)) raw = "[]";

    JsonDocument oldDoc;
    if (deserializeJson(oldDoc, raw) || !oldDoc.is<JsonArray>()) {
        oldDoc.clear();
        oldDoc.to<JsonArray>();
    }

    JsonDocument newDoc;
    JsonArray out = newDoc.to<JsonArray>();
    JsonObject first = out.add<JsonObject>();
    productToJson(first, product);

    size_t copied = 1;
    for (JsonObject obj : oldDoc.as<JsonArray>()) {
        if (copied >= OFF_CACHE_MAX_ITEMS) break;
        if (String(obj["barcode"] | "") == product.barcode) continue;
        JsonObject dst = out.add<JsonObject>();
        dst.set(obj);
        copied++;
    }

    String serialized;
    serializeJson(newDoc, serialized);
    filesystem->writeFileAtomic(OFF_CACHE_PATH, serialized);
}
