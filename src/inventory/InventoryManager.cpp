#include "InventoryManager.h"

InventoryManager::InventoryManager(InventoryStorage &storage) : storage(storage) {}

bool InventoryManager::begin() {
    storage.loadRemoved(_recentlyRemoved);
    pruneOldRemoved();
    return storage.load(inventory);
}

bool InventoryManager::addItem(const InventoryItem &item) {
    Serial.printf("[Inv] addItem lb='%s' bc='%s' name='%s'\n",
                  item.labelBarcode.c_str(), item.barcode.c_str(), item.name.c_str());
    inventory.push_back(item);
    bool ok = storage.save(inventory);
    Serial.printf("[Inv] addItem -> saved=%d total=%u\n", ok, (unsigned)inventory.size());
    return ok;
}

bool InventoryManager::updateByLabel(const String &labelBarcode, const InventoryItem &updated) {
    Serial.printf("[Inv] updateByLabel lb='%s' name='%s'\n",
                  labelBarcode.c_str(), updated.name.c_str());
    for (auto &it : inventory) {
        if (it.labelBarcode == labelBarcode) {
            it = updated;
            it.labelBarcode = labelBarcode;  // preserve key
            bool ok = storage.save(inventory);
            Serial.printf("[Inv] updateByLabel -> found, saved=%d\n", ok);
            return ok;
        }
    }
    Serial.printf("[Inv] updateByLabel -> NOT FOUND lb='%s'\n", labelBarcode.c_str());
    return false;
}

bool InventoryManager::removeByLabel(const String &labelBarcode) {
    Serial.printf("[Inv] removeByLabel lb='%s'\n", labelBarcode.c_str());
    for (auto it = inventory.begin(); it != inventory.end(); ++it) {
        if (it->labelBarcode == labelBarcode) {
            Serial.printf("[Inv] removeByLabel -> found '%s', recording removal\n", it->name.c_str());
            recordRemoval(*it);
            inventory.erase(it);
            bool ok = storage.save(inventory);
            Serial.printf("[Inv] removeByLabel -> saved=%d total=%u\n", ok, (unsigned)inventory.size());
            return ok;
        }
    }
    Serial.printf("[Inv] removeByLabel -> NOT FOUND lb='%s'\n", labelBarcode.c_str());
    return false;
}

bool InventoryManager::removeByLabelPermanent(const String &labelBarcode) {
    Serial.printf("[Inv] removeByLabelPermanent lb='%s'\n", labelBarcode.c_str());
    for (auto it = inventory.begin(); it != inventory.end(); ++it) {
        if (it->labelBarcode == labelBarcode) {
            Serial.printf("[Inv] removeByLabelPermanent -> found '%s', no buffer\n", it->name.c_str());
            inventory.erase(it);
            bool ok = storage.save(inventory);
            Serial.printf("[Inv] removeByLabelPermanent -> saved=%d total=%u\n", ok, (unsigned)inventory.size());
            return ok;
        }
    }
    Serial.printf("[Inv] removeByLabelPermanent -> NOT FOUND lb='%s'\n", labelBarcode.c_str());
    return false;
}

bool InventoryManager::removeByBarcode(const String &barcode) {
    for (auto it = inventory.begin(); it != inventory.end(); ++it) {
        if (it->barcode == barcode) {
            recordRemoval(*it);
            inventory.erase(it);
            return storage.save(inventory);
        }
    }
    return false;
}

const std::vector<InventoryItem> &InventoryManager::items() const {
    return inventory;
}

bool InventoryManager::hasLabel(const String &labelBarcode) const {
    for (const auto &it : inventory)
        if (it.labelBarcode == labelBarcode) return true;
    return false;
}

const InventoryItem *InventoryManager::findRecent(const String &barcode) const {
    time_t now = time(nullptr);
    for (const RemovedItem &ri : _recentlyRemoved) {
        if (ri.item.barcode == barcode) {
            time_t age = now - ri.removedAt;
            if (age >= 0 && age < REMOVED_TTL_SECS) return &ri.item;
        }
    }
    return nullptr;
}

const InventoryItem *InventoryManager::findRecentByLabel(const String &labelBarcode) const {
    for (const RemovedItem &ri : _recentlyRemoved)
        if (ri.item.labelBarcode == labelBarcode) return &ri.item;
    return nullptr;
}

bool InventoryManager::restoreByLabel(const String &labelBarcode, const InventoryItem &restored) {
    for (auto it = _recentlyRemoved.begin(); it != _recentlyRemoved.end(); ++it) {
        if (it->item.labelBarcode == labelBarcode) {
            _recentlyRemoved.erase(it);
            storage.saveRemoved(_recentlyRemoved);
            break;
        }
    }
    inventory.push_back(restored);
    return storage.save(inventory);
}

void InventoryManager::pruneOldRemoved() {
    time_t now = time(nullptr);
    bool changed = false;
    for (auto it = _recentlyRemoved.begin(); it != _recentlyRemoved.end(); ) {
        time_t age = now - it->removedAt;
        if (age < 0 || age >= REMOVED_TTL_SECS) {
            it = _recentlyRemoved.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) storage.saveRemoved(_recentlyRemoved);
}

void InventoryManager::recordRemoval(const InventoryItem &item) {
    // Remove any previous entry for the same barcode to avoid duplicates
    for (auto it = _recentlyRemoved.begin(); it != _recentlyRemoved.end(); ++it) {
        if (it->item.barcode == item.barcode) {
            _recentlyRemoved.erase(it);
            break;
        }
    }
    RemovedItem ri;
    ri.item      = item;
    ri.removedAt = time(nullptr);
    _recentlyRemoved.push_back(ri);
    storage.saveRemoved(_recentlyRemoved);
}
