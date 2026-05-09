#include "EventBus.h"

bool EventBus::publish(const AppEvent &event) {
    if (count >= QUEUE_SIZE) return false;
    queue[tail] = event;
    tail = (tail + 1) % QUEUE_SIZE;
    count++;
    return true;
}

bool EventBus::poll(AppEvent &event) {
    if (count == 0) return false;
    event = queue[head];
    head = (head + 1) % QUEUE_SIZE;
    count--;
    return true;
}

void EventBus::clear() {
    head = 0;
    tail = 0;
    count = 0;
}
