#include "InventoryStorage.h"

InventoryStorage::InventoryStorage(JsonStorage &jsonStorage) : json(jsonStorage) {}

bool InventoryStorage::load(std::vector<InventoryItem> &items) {
    JsonDocument doc;
    bool ok = json.loadDocument("/inventory.json", doc, "[]");
    items.clear();
    if (!doc.is<JsonArray>()) return false;
    for (JsonObject obj : doc.as<JsonArray>()) {
        InventoryItem item;
        item.barcode = obj["barcode"] | "";
        item.name = obj["name"] | "";
        item.brand = obj["brand"] | "";
        item.category = obj["category"] | "";
        item.expiryDate = obj["expiryDate"] | "";
        item.addedDate = obj["addedDate"] | "";
        item.quantity = obj["quantity"] | 0;
        item.labelBarcode = obj["labelBarcode"] | "";
        items.push_back(item);
    }
    return ok;
}

bool InventoryStorage::save(const std::vector<InventoryItem> &items) {
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();
    for (const InventoryItem &item : items) {
        JsonObject obj = array.add<JsonObject>();
        obj["barcode"] = item.barcode;
        obj["name"] = item.name;
        obj["brand"] = item.brand;
        obj["category"] = item.category;
        obj["expiryDate"] = item.expiryDate;
        obj["addedDate"] = item.addedDate;
        obj["quantity"] = item.quantity;
        obj["labelBarcode"] = item.labelBarcode;
    }
    return json.saveDocument("/inventory.json", doc);
}
