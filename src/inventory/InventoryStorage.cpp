#include "InventoryStorage.h"

InventoryStorage::InventoryStorage(JsonStorage &jsonStorage) : json(jsonStorage) {}

// Migrates any "YYYY-MM-DD" date strings to "DD.MM.YYYY" on first load.
static String normDate(const String &s) {
    if (s.length() == 10 && s[4] == '-' && s[7] == '-')
        return s.substring(8, 10) + "." + s.substring(5, 7) + "." + s.substring(0, 4);
    return s;
}

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
        item.subcategory = obj["subcategory"] | "";
        item.expiryDate = normDate(obj["expiryDate"] | "");
        item.addedDate  = normDate(obj["addedDate"]  | "");
        item.quantity     = obj["quantity"]     | 0;
        item.unit         = obj["unit"]         | "";
        item.labelBarcode = obj["labelBarcode"] | "";
        item.location     = obj["location"]     | "";
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
        obj["subcategory"] = item.subcategory;
        obj["expiryDate"] = item.expiryDate;
        obj["addedDate"] = item.addedDate;
        obj["quantity"]     = item.quantity;
        obj["unit"]         = item.unit;
        obj["labelBarcode"] = item.labelBarcode;
        obj["location"]     = item.location;
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
        ri.item.subcategory    = obj["subcategory"] | "";
        ri.item.expiryDate     = normDate(obj["expiryDate"] | "");
        ri.item.addedDate      = normDate(obj["addedDate"]  | "");
        ri.item.quantity       = obj["quantity"]     | 0;
        ri.item.unit           = obj["unit"]         | "";
        ri.item.labelBarcode   = obj["labelBarcode"] | "";
        ri.item.location       = obj["location"]     | "";
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
        obj["subcategory"]  = ri.item.subcategory;
        obj["expiryDate"]   = ri.item.expiryDate;
        obj["addedDate"]    = ri.item.addedDate;
        obj["quantity"]     = ri.item.quantity;
        obj["unit"]         = ri.item.unit;
        obj["labelBarcode"] = ri.item.labelBarcode;
        obj["location"]     = ri.item.location;
    }
    return json.saveDocument("/removed_items.json", doc);
}
