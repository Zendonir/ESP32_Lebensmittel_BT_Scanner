#include "OpenFoodFacts.h"
#include "../core/Logger.h"
#include <ArduinoJson.h>

OpenFoodFacts::OpenFoodFacts(ApiClient &client) : api(client) {}

bool OpenFoodFacts::fetchProduct(const String &barcode, ProductInfo &product) {
    ApiResponse response = api.get("https://world.openfoodfacts.org/api/v3/product/" + barcode + ".json");
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
    return !product.name.isEmpty();
}
