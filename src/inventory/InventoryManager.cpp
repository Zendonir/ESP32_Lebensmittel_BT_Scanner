#include "InventoryManager.h"

InventoryManager::InventoryManager(InventoryStorage &storage) : storage(storage) {}

bool InventoryManager::begin() {
    return storage.load(inventory);
}

bool InventoryManager::addItem(const InventoryItem &item) {
    inventory.push_back(item);
    return storage.save(inventory);
}

bool InventoryManager::removeByLabel(const String &labelBarcode) {
    for (auto it = inventory.begin(); it != inventory.end(); ++it) {
        if (it->labelBarcode == labelBarcode) {
            inventory.erase(it);
            return storage.save(inventory);
        }
    }
    return false;
}

bool InventoryManager::removeByBarcode(const String &barcode) {
    for (auto it = inventory.begin(); it != inventory.end(); ++it) {
        if (it->barcode == barcode) {
            inventory.erase(it);
            return storage.save(inventory);
        }
    }
    return false;
}

const std::vector<InventoryItem> &InventoryManager::items() const {
    return inventory;
}
