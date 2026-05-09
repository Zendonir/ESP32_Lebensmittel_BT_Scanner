#pragma once

#include <Arduino.h>
#include <vector>
#include "InventoryStorage.h"
#include "../models/InventoryItem.h"

class InventoryManager {
public:
    explicit InventoryManager(InventoryStorage &storage);
    bool begin();
    bool addItem(const InventoryItem &item);
    bool removeByLabel(const String &labelBarcode);
    const std::vector<InventoryItem> &items() const;

private:
    InventoryStorage &storage;
    std::vector<InventoryItem> inventory;
};
