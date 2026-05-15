#pragma once

#include <Arduino.h>
#include <vector>
#include <time.h>
#include "InventoryStorage.h"
#include "../models/InventoryItem.h"

static constexpr time_t REMOVED_TTL_SECS = 48 * 3600;

class InventoryManager {
public:
    explicit InventoryManager(InventoryStorage &storage);
    bool begin();
    bool addItem(const InventoryItem &item);
    bool removeByLabel(const String &labelBarcode);
    bool removeByBarcode(const String &barcode);
    const std::vector<InventoryItem> &items() const;

    // 48 h recently-removed buffer
    const InventoryItem *findRecent(const String &barcode) const;
    void pruneOldRemoved();

private:
    void recordRemoval(const InventoryItem &item);

    InventoryStorage          &storage;
    std::vector<InventoryItem> inventory;
    std::vector<RemovedItem>   _recentlyRemoved;
};
