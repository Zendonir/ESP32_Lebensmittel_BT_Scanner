#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "config.h"

class Audio {
public:
    Audio();
    void init();
    void playTone(uint16_t frequency, uint16_t duration_ms);
    void playSound(const uint8_t *data, size_t size);
    void setVolume(uint8_t volume);   // 0–100
    void stop();

private:
    static void toneTask(void *param);

    uint8_t      volume_level;
    bool         is_initialized;
    QueueHandle_t _queue;
};

extern Audio audio_obj;

#endif
