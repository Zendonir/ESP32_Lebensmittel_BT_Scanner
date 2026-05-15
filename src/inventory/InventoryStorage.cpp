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

bool InventoryStorage::loadRemoved(std::vector<RemovedItem> &items) {
    JsonDocument doc;
    bool ok = json.loadDocument("/removed_items.json", doc, "[]");
    items.clear();
    if (!doc.is<JsonArray>()) return false;
    for (JsonObject obj : doc.as<JsonArray>()) {
        RemovedItem ri;
        ri.removedAt           = (time_t)(obj["removedAt"] | (long)0);
        ri.item.barcode        = obj["barcode"]    | "";
        ri.item.name           = obj["name"]       | "";
        ri.item.brand          = obj["brand"]      | "";
        ri.item.category       = obj["category"]   | "";
        ri.item.expiryDate     = obj["expiryDate"] | "";
        ri.item.addedDate      = obj["addedDate"]  | "";
        ri.item.quantity       = obj["quantity"]   | 0;
        ri.item.labelBarcode   = obj["labelBarcode"] | "";
        items.push_back(ri);
    }
    return ok;
}

bool InventoryStorage::saveRemoved(const std::vector<RemovedItem> &items) {
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();
    for (const RemovedItem &ri : items) {
        JsonObject obj = array.add<JsonObject>();
        obj["removedAt"]    = (long)ri.removedAt;
        obj["barcode"]      = ri.item.barcode;
        obj["name"]         = ri.item.name;
        obj["brand"]        = ri.item.brand;
        obj["category"]     = ri.item.category;
        obj["expiryDate"]   = ri.item.expiryDate;
        obj["addedDate"]    = ri.item.addedDate;
        obj["quantity"]     = ri.item.quantity;
        obj["labelBarcode"] = ri.item.labelBarcode;
    }
    return json.saveDocument("/removed_items.json", doc);
}
