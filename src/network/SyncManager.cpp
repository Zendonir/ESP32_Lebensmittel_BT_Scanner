#include "SyncManager.h"

void SyncManager::enqueue(const SyncEvent &event) {
    queue.push_back(event);
}

void SyncManager::loop() {
    // REST retry worker hook. Keep non-blocking; process one event per call.
}

size_t SyncManager::pending() const { return queue.size(); }
