#pragma once

#include <Arduino.h>
#include <vector>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "InventoryStorage.h"
#include "../models/InventoryItem.h"
#include <map>

static constexpr time_t REMOVED_TTL_SECS = 48 * 3600;

// RAII scoped lock — defined here so template methods below can use it.
struct InvLock {
    SemaphoreHandle_t m;
    explicit InvLock(SemaphoreHandle_t mm) : m(mm) { if (m) xSemaphoreTakeRecursive(m, portMAX_DELAY); }
    ~InvLock()                                       { if (m) xSemaphoreGiveRecursive(m); }
};

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

    // Merge duplicate entries (same name+barcode+expiryDate+location+unit) into one.
    // Returns the number of entries removed. removedLabels receives their labelBarcodes.
    int compact(std::vector<String> &removedLabels);

    // ------- Lock-free iterators (no heap copy) --------------------------------
    // Count items matching pred — lock held for full iteration (no allocation).
    template<typename Pred>
    int countWhere(Pred pred) const {
        InvLock lk(_mutex);
        int n = 0;
        for (const auto &item : inventory) if (pred(item)) n++;
        return n;
    }

    // Filtered copy — one allocation, only matching items (smaller than full copy).
    template<typename Pred>
    std::vector<InventoryItem> copyWhere(Pred pred) const {
        InvLock lk(_mutex);
        std::vector<InventoryItem> result;
        for (const auto &item : inventory) if (pred(item)) result.push_back(item);
        return result;
    }

    // Visit each item under lock — avoids any heap allocation.
    // fn must not call back into InventoryManager (recursive mutex guards re-entry).
    template<typename Fn>
    void forEachItem(Fn fn) const {
        InvLock lk(_mutex);
        for (const auto &item : inventory) fn(item);
    }
    // ---------------------------------------------------------------------------

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
