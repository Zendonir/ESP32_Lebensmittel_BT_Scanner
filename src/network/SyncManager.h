#pragma once

#include <Arduino.h>
#include <vector>

struct SyncEvent {
    String type;
    String payload;
    uint8_t retries = 0;
};

class SyncManager {
public:
    void enqueue(const SyncEvent &event);
    void loop();
    size_t pending() const;

private:
    std::vector<SyncEvent> queue;
};
