#pragma once

#include <Arduino.h>
#include <vector>
#include <time.h>
#include "../models/InventoryItem.h"
#include "../storage/JsonStorage.h"

struct RemovedItem {
    InventoryItem item;
    time_t        removedAt = 0;
};

class InventoryStorage {
public:
    explicit InventoryStorage(JsonStorage &jsonStorage);
    bool load(std::vector<InventoryItem> &items);
    bool save(const std::vector<InventoryItem> &items);
    bool loadRemoved(std::vector<RemovedItem> &items);
    bool saveRemoved(const std::vector<RemovedItem> &items);

private:
    JsonStorage &json;
};
