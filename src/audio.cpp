#include "audio.h"
#include <driver/i2s_std.h>
#include <math.h>

Audio audio_obj;
i2s_chan_handle_t tx_chan = NULL;

Audio::Audio() : volume_level(100), is_initialized(false) {}

void Audio::init() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &tx_chan, NULL);

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_BCLK,
            .ws = (gpio_num_t)I2S_LRCK,
            .dout = (gpio_num_t)I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {0},
        },
    };

    i2s_channel_init_std_mode(tx_chan, &std_cfg);
    i2s_channel_enable(tx_chan);
    Serial.println("[Audio] I2S initialized");
    is_initialized = true;
}

void Audio::playTone(uint16_t frequency, uint16_t duration_ms) {
    if (!is_initialized || !tx_chan) {
        Serial.println("[Audio] Not initialized");
        return;
    }

    uint32_t sample_rate = 44100;
    uint16_t samples = (sample_rate * duration_ms) / 1000;

    int16_t *buffer = (int16_t *)malloc(samples * 4);
    if (!buffer) return;

    float phase_increment = (2.0f * M_PI * frequency) / sample_rate;
    float phase = 0;

    for (uint16_t i = 0; i < samples; i++) {
        int16_t sample = (int16_t)(sinf(phase) * 32767.0f * volume_level / 100.0f);
        buffer[i * 2] = sample;
        buffer[i * 2 + 1] = sample;
        phase += phase_increment;
    }

    size_t bytes_written = 0;
    i2s_channel_write(tx_chan, buffer, samples * 4, &bytes_written, 1000);
    Serial.printf("[Audio] Tone %dHz for %dms\n", frequency, duration_ms);
    free(buffer);
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
