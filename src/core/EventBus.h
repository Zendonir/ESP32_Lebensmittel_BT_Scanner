#pragma once

#include <Arduino.h>

enum class EventType {
    NONE,
    BARCODE_SCANNED,
    LABEL_SCANNED,
    PRODUCT_FETCHED,
    PRODUCT_FETCH_FAILED,
    INVENTORY_CHANGED,
    PRINT_DONE,
    PRINT_FAILED,
    TOUCH_ACTIVITY,
    TIMEOUT
};

struct AppEvent {
    EventType type = EventType::NONE;
    String payload;
};

class EventBus {
public:
    bool publish(const AppEvent &event);
    bool poll(AppEvent &event);
    void clear();

private:
    static constexpr size_t QUEUE_SIZE = 12;
    AppEvent queue[QUEUE_SIZE];
    size_t head = 0;
    size_t tail = 0;
    size_t count = 0;
};
