#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>
#include "config.h"

class Audio {
public:
    Audio();
    void init();
    void playTone(uint16_t frequency, uint16_t duration_ms);
    void playSound(const uint8_t *data, size_t size);
    void setVolume(uint8_t volume);
    void stop();

private:
    uint8_t volume_level;
    bool is_initialized;

    void generateTone(uint16_t frequency, uint16_t duration_ms, int16_t *buffer, size_t buffer_size);
};

extern Audio audio_obj;

#endif
