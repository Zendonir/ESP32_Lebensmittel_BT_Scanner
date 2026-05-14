#include "OpenFoodFacts.h"
#include "../core/Logger.h"
#include <ArduinoJson.h>

namespace {
constexpr const char *OFF_CACHE_PATH = "/off_cache.json";
constexpr size_t OFF_CACHE_MAX_ITEMS = 50;

void productFromJson(const String &barcode, JsonObject obj, ProductInfo &product) {
    product.barcode    = obj["barcode"]   | barcode;
    product.name       = obj["name"]      | "";
    product.brand      = obj["brand"]     | "";
    product.quantity   = obj["quantity"]  | "";
    product.category   = obj["category"]  | "";
    product.nutriscore = obj["nutriscore"]| "";
    product.labels.clear();
    for (JsonVariant label : obj["labels"].as<JsonArray>())
        product.labels.push_back(label.as<String>());
}

void productToJson(JsonObject obj, const ProductInfo &product) {
    obj["barcode"]    = product.barcode;
    obj["name"]       = product.name;
    obj["brand"]      = product.brand;
    obj["quantity"]   = product.quantity;
    obj["category"]   = product.category;
    obj["nutriscore"] = product.nutriscore;
    JsonArray labels  = obj["labels"].to<JsonArray>();
    for (const String &label : product.labels) labels.add(label);
    obj["cachedAt"]   = millis();
}
}

OpenFoodFacts::OpenFoodFacts(ApiClient &client, LittleFSManager *cacheFs) : api(client), filesystem(cacheFs) {}

bool OpenFoodFacts::fetchProduct(const String &barcode, ProductInfo &product) {
    if (loadCachedProduct(barcode, product)) {
        Logger::info("OpenFoodFacts", String("Cache hit: ") + barcode);
        return true;
    }

    // Request only the fields we actually use – drops response from ~200 KB
    // down to ~1-2 KB, making the fetch 5-10× faster on slow connections.
    String url = "https://world.openfoodfacts.org/api/v2/product/" + barcode
               + "?fields=product_name,product_name_de,brands,quantity"
                 ",categories_tags,nutriscore_grade,labels_tags";

    ApiResponse response = api.get(url);
    if (response.status <= 0) {
        Logger::warn("OpenFoodFacts", "First attempt failed, retrying...");
        delay(1000);
        response = api.get(url);
    }
    if (response.status != 200) {
        Logger::warn("OpenFoodFacts", String("HTTP status ") + response.status);
        return false;
    }

    // JSON filter: skip everything except the fields we requested.
    // This keeps the working-set tiny even if the server sends extra data.
    JsonDocument filter;
    filter["status"]                            = true;
    filter["product"]["product_name_de"]        = true;
    filter["product"]["product_name"]           = true;
    filter["product"]["brands"]                 = true;
    filter["product"]["quantity"]               = true;
    filter["product"]["nutriscore_grade"]       = true;
    filter["product"]["categories_tags"][0]     = true;
    filter["product"]["labels_tags"][0]         = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, response.body, DeserializationOption::Filter(filter));

    if (err || doc["status"].as<int>() != 1) {
        Logger::warn("OpenFoodFacts", String("Parse/status error: ") + (err ? err.c_str() : "status!=1"));
        return false;
    }

    JsonObject p = doc["product"];
    product.barcode = barcode;

    // Prefer German name, fall back to generic product_name
    String nameDe = p["product_name_de"] | "";
    product.name  = nameDe.isEmpty() ? (p["product_name"] | "") : nameDe;

    product.brand      = p["brands"]          | "";
    product.quantity   = p["quantity"]         | "";
    product.nutriscore = p["nutriscore_grade"] | "";

    // First category: strip language prefix ("en:dairy" → "dairy")
    String cat = p["categories_tags"][0] | "";
    int colon  = cat.indexOf(':');
    product.category = (colon >= 0) ? cat.substring(colon + 1) : cat;

    product.labels.clear();
    for (JsonVariant lbl : p["labels_tags"].as<JsonArray>())
        product.labels.push_back(lbl.as<String>());

    bool ok = !product.name.isEmpty();
    if (ok) cacheProduct(product);
    Logger::info("OpenFoodFacts", String("Fetched: ") + product.name + " (" + product.brand + ")");
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
