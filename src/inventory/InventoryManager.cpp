#include "InventoryManager.h"

InventoryManager::InventoryManager(InventoryStorage &storage) : storage(storage) {}

bool InventoryManager::begin() {
    storage.loadRemoved(_recentlyRemoved);
    pruneOldRemoved();
    return storage.load(inventory);
}

bool InventoryManager::addItem(const InventoryItem &item) {
    inventory.push_back(item);
    return storage.save(inventory);
}

bool InventoryManager::removeByLabel(const String &labelBarcode) {
    for (auto it = inventory.begin(); it != inventory.end(); ++it) {
        if (it->labelBarcode == labelBarcode) {
            recordRemoval(*it);
            inventory.erase(it);
            return storage.save(inventory);
        }
    }
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
