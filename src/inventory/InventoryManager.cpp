#include "InventoryManager.h"

// RAII scoped lock for the recursive inventory mutex.
namespace {
struct InvLock {
    SemaphoreHandle_t m;
    explicit InvLock(SemaphoreHandle_t mm) : m(mm) { if (m) xSemaphoreTakeRecursive(m, portMAX_DELAY); }
    ~InvLock()                                       { if (m) xSemaphoreGiveRecursive(m); }
};
}

InventoryManager::InventoryManager(InventoryStorage &storage) : storage(storage) {}

bool InventoryManager::begin() {
    // Created here; begin() runs once at startup before any other task touches us.
    if (!_mutex) _mutex = xSemaphoreCreateRecursiveMutex();
    storage.loadRemoved(_recentlyRemoved);
    pruneOldRemoved();
    return storage.load(inventory);
}

bool InventoryManager::addItem(const InventoryItem &item) {
    InvLock lk(_mutex);
    Serial.printf("[Inv] addItem lb='%s' bc='%s' name='%s'\n",
                  item.labelBarcode.c_str(), item.barcode.c_str(), item.name.c_str());
    inventory.push_back(item);
    bool ok = storage.save(inventory);
    Serial.printf("[Inv] addItem -> saved=%d total=%u\n", ok, (unsigned)inventory.size());
    return ok;
}

bool InventoryManager::updateByLabel(const String &labelBarcode, const InventoryItem &updated) {
    InvLock lk(_mutex);
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
    InvLock lk(_mutex);
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
    InvLock lk(_mutex);
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
    InvLock lk(_mutex);
    for (auto it = inventory.begin(); it != inventory.end(); ++it) {
        if (it->barcode == barcode) {
            recordRemoval(*it);
            inventory.erase(it);
            return storage.save(inventory);
        }
    }
    return false;
}

std::vector<InventoryItem> InventoryManager::items() const {
    InvLock lk(_mutex);
    return inventory;   // copy under lock – safe to iterate after unlock
}

size_t InventoryManager::count() const {
    InvLock lk(_mutex);
    return inventory.size();
}

bool InventoryManager::hasLabel(const String &labelBarcode) const {
    InvLock lk(_mutex);
    for (const auto &it : inventory)
        if (it.labelBarcode == labelBarcode) return true;
    return false;
}

const InventoryItem *InventoryManager::findRecent(const String &barcode) const {
    InvLock lk(_mutex);
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
    InvLock lk(_mutex);
    time_t now = time(nullptr);
    for (const RemovedItem &ri : _recentlyRemoved) {
        if (ri.item.labelBarcode == labelBarcode) {
            time_t age = now - ri.removedAt;          // honour the 48h-Vertrag
            if (age >= 0 && age < REMOVED_TTL_SECS) return &ri.item;
        }
    }
    return nullptr;
}

bool InventoryManager::restoreByLabel(const String &labelBarcode, const InventoryItem &restored) {
    InvLock lk(_mutex);
    for (auto it = _recentlyRemoved.begin(); it != _recentlyRemoved.end(); ++it) {
        if (it->item.labelBarcode == labelBarcode) {
            _recentlyRemoved.erase(it);
            storage.saveRemoved(_recentlyRemoved);
            break;
        }
    }
    // Dedup: ein Label ist eindeutig. Existiert es schon im Inventar,
    // in-place aktualisieren statt ein Duplikat anzulegen (sonst nicht mehr
    // editier-/löschbar in der UI + MySQL-UNIQUE-Konflikt beim Sync).
    for (auto &it : inventory) {
        if (it.labelBarcode == labelBarcode) {
            it = restored;
            it.labelBarcode = labelBarcode;
            return storage.save(inventory);
        }
    }
    inventory.push_back(restored);
    return storage.save(inventory);
}

int InventoryManager::compact(std::vector<String> &removedLabels) {
    InvLock lk(_mutex);
    removedLabels.clear();

    // Group by (name + barcode + expiryDate + location + unit).
    // First occurrence of each key is kept; subsequent duplicates are merged into it.
    std::map<String, int> seen;   // key → index in compacted
    std::vector<InventoryItem> compacted;

    for (const auto &item : inventory) {
        String key = item.name + "|" + item.barcode + "|" +
                     item.expiryDate + "|" + item.location + "|" + item.unit;
        auto it = seen.find(key);
        if (it == seen.end()) {
            seen[key] = (int)compacted.size();
            compacted.push_back(item);
        } else {
            compacted[it->second].quantity += item.quantity;
            removedLabels.push_back(item.labelBarcode);
        }
    }

    if (removedLabels.empty()) return 0;

    Serial.printf("[Inv] compact: %u → %u entries (%d merged)\n",
                  (unsigned)inventory.size(), (unsigned)compacted.size(), (int)removedLabels.size());
    inventory = std::move(compacted);
    storage.save(inventory);
    return (int)removedLabels.size();
}

void InventoryManager::pruneOldRemoved() {
    InvLock lk(_mutex);
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
    InvLock lk(_mutex);   // recursive: callers (removeByLabel/removeByBarcode) already hold it
    // Dedup nach labelBarcode (nicht barcode): zwei physische Artikel desselben
    // Produkts haben dieselbe barcode, aber unterschiedliche Label. Würde nach
    // barcode dedupliziert, ginge beim Auslagern des zweiten der Restore-Eintrag
    // des ersten verloren. Restore läuft über das Label – also auch hier so keyen.
    for (auto it = _recentlyRemoved.begin(); it != _recentlyRemoved.end(); ++it) {
        if (it->item.labelBarcode == item.labelBarcode) {
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
