#pragma once

#include <Arduino.h>
#include <vector>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "InventoryStorage.h"
#include "../models/InventoryItem.h"

static constexpr time_t REMOVED_TTL_SECS = 48 * 3600;

// Thread-safe: the same instance is accessed from App::loop() (core 1) and the
// async web handlers (AsyncTCP task). All public methods lock a recursive mutex.
// items() returns a SNAPSHOT (copy) so callers can iterate without holding the
// lock while another context mutates the underlying vector (which would
// otherwise reallocate and invalidate the iterators → use-after-free).
class InventoryManager {
public:
    explicit InventoryManager(InventoryStorage &storage);
    bool begin();
    bool addItem(const InventoryItem &item);
    // Updates existing item in-place by labelBarcode; returns false if not found.
    bool updateByLabel(const String &labelBarcode, const InventoryItem &updated);
    bool removeByLabel(const String &labelBarcode);
    // Removes without recording to the 48h buffer (web UI permanent delete).
    bool removeByLabelPermanent(const String &labelBarcode);
    bool removeByBarcode(const String &barcode);
    std::vector<InventoryItem> items() const;   // locked snapshot (copy)
    size_t count() const;                        // locked size (cheap, no copy)
    bool hasLabel(const String &labelBarcode) const;

    // 48 h recently-removed buffer
    const InventoryItem *findRecent(const String &barcode) const;
    const InventoryItem *findRecentByLabel(const String &labelBarcode) const;
    bool                 restoreByLabel(const String &labelBarcode, const InventoryItem &restored);
    void pruneOldRemoved();

private:
    void recordRemoval(const InventoryItem &item);

    InventoryStorage          &storage;
    std::vector<InventoryItem> inventory;
    std::vector<RemovedItem>   _recentlyRemoved;
    SemaphoreHandle_t          _mutex = nullptr;   // recursive; guards both vectors
};
