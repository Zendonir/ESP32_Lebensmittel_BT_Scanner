#pragma once

#include <Arduino.h>
#include <vector>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "../models/ProductInfo.h"
#include "../models/InventoryItem.h"
#include "../storage/LittleFSManager.h"

struct SyncEvent {
    String   type;       // "ADD", "REMOVE_LABEL", "REMOVE_BARCODE"
    String   payload;    // JSON string (includes device/household context)
    uint8_t  retries  = 0;
    uint32_t createdMs = 0;
};

class SyncManager {
public:
    void   begin();
    void   loadConfig();   // public so WebInterface can reload after a settings save
    void   loop();
    void   enqueue(const String &type, const String &jsonPayload);
    size_t pending() const;             // locked
    void   clearQueue();
    String getQueueJson() const;
    bool   testConnection(String &outMsg);
    time_t getLastSync()   const { return _lastSyncTime; }
    bool   wasLastSyncOk() const { return _lastSyncOk; }
    bool   hasConfig()     const { return !_ip.isEmpty(); }

    // Product cache — MySQL product_cache table
    bool pushProduct(const ProductInfo &product);
    bool fetchProductFromMySQL(const String &barcode, ProductInfo &out);
    int  pullProductsToCache(LittleFSManager &fs);

    // Multi-device inventory sync
    // Pull items added by other devices (different device_name, same household).
    // Returns items not currently in our local inventory.
    std::vector<InventoryItem> pullInventory(const String &household, const String &ourDevice);
    // Pull label_barcodes that other devices removed (tombstones from removed_items table).
    std::vector<String> pullRemovals(const String &household, const String &ourDevice);

private:
    String _ip, _user, _pass;
    std::vector<SyncEvent> _queue;
    SemaphoreHandle_t _queueMutex = nullptr;  // guards _queue (core1 loop/enqueue vs web TCP task)
    volatile time_t   _lastSyncTime  = 0;
    volatile bool     _lastSyncOk    = false;
    uint32_t _lastAttemptMs = 0;

    static constexpr uint32_t RETRY_INTERVAL_MS = 30000;
    static constexpr uint8_t  MAX_RETRIES        = 5;

    void saveQueue();    // caller must hold _queueMutex
    void loadQueue();
    void dropFrontIfMatches(const String &payload);   // locks internally
    bool execDirectMySQL(const String &sql);

    static String labelsToString(const std::vector<String> &labels);
    static void   labelsFromString(const String &s, std::vector<String> &out);
};

extern SyncManager sync_manager;
