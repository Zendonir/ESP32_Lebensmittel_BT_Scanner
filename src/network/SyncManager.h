#pragma once

#include <Arduino.h>
#include <vector>
#include <time.h>

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
    size_t pending() const { return _queue.size(); }
    void   clearQueue();
    String getQueueJson() const;
    bool   testConnection(String &outMsg);
    time_t getLastSync()   const { return _lastSyncTime; }
    bool   wasLastSyncOk() const { return _lastSyncOk; }

private:
    String _ip, _user, _pass;
    std::vector<SyncEvent> _queue;
    time_t   _lastSyncTime  = 0;
    bool     _lastSyncOk    = false;
    uint32_t _lastAttemptMs = 0;

    static constexpr uint32_t RETRY_INTERVAL_MS = 30000;
    static constexpr uint8_t  MAX_RETRIES        = 5;

    void saveQueue();
    void loadQueue();
    bool postJson(const String &json, int *outCode = nullptr);
};

extern SyncManager sync_manager;
