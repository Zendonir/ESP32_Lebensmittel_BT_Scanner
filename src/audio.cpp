#include "audio.h"

Audio audio_obj;

Audio::Audio() : volume_level(100), is_initialized(false) {}

void Audio::init() {
    Serial.println("[Audio] I2S init (placeholder)");
    is_initialized = true;
}

void Audio::playTone(uint16_t frequency, uint16_t duration_ms) {
    Serial.printf("[Audio] Tone %dHz for %dms\n", frequency, duration_ms);
}

void Audio::playSound(const uint8_t *data, size_t size) {
    Serial.printf("[Audio] Play sound (%d bytes)\n", size);
}

void Audio::setVolume(uint8_t volume) {
    volume_level = constrain(volume, 0, 100);
    Serial.printf("[Audio] Volume: %d%%\n", volume_level);
}

void Audio::stop() {
    Serial.println("[Audio] Stop");
}
