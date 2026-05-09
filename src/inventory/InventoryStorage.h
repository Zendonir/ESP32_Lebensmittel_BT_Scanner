#pragma once

#include <Arduino.h>
#include <vector>
#include "../models/InventoryItem.h"
#include "../storage/JsonStorage.h"

class InventoryStorage {
public:
    explicit InventoryStorage(JsonStorage &jsonStorage);
    bool load(std::vector<InventoryItem> &items);
    bool save(const std::vector<InventoryItem> &items);

private:
    JsonStorage &json;
};
