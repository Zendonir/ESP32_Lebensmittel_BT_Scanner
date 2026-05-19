#ifndef AUDIO_H
#define AUDIO_H

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "config.h"

class Audio {
public:
    Audio();
    void init(TwoWire &wire);

    // Core playback
    void playTone(uint16_t frequency, uint16_t duration_ms, uint8_t vol_override = 0);
    void stopTone();

    // Convenience signals
    void playSuccessTone();
    void playErrorTone();
    void playWarningTone();
    void playStartupTone();
    void playClickTone();
    void playSwipeTone();
    void playCheckoutTone();

    // Volume: 0–100 %
    void setVolume(uint8_t volume);

    // Legacy stubs (no-op / alias)
    void playSound(const uint8_t *data, size_t size);
    void stop();

private:
    static void toneTask(void *param);

    uint8_t       volume_level;
    bool          is_initialized;
    QueueHandle_t _queue;
    Preferences   _prefs;
};

extern Audio audio_obj;

#endif
