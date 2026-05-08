#include "audio.h"

Audio audio_obj;

Audio::Audio() : volume_level(100), is_initialized(false) {}

void Audio::init() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRCK,
        .data_out_num = I2S_DOUT,
        .data_in_num = I2S_DIN
    };

    i2s_set_pin(I2S_NUM, &pin_config);

    is_initialized = true;
}

void Audio::playTone(uint16_t frequency, uint16_t duration_ms) {
    if (!is_initialized) return;

    const size_t buffer_size = (44100 / 1000) * duration_ms;
    int16_t *buffer = (int16_t *)malloc(buffer_size * sizeof(int16_t));

    if (!buffer) return;

    generateTone(frequency, duration_ms, buffer, buffer_size);

    size_t bytes_written;
    i2s_write(I2S_NUM, buffer, buffer_size * sizeof(int16_t), &bytes_written, portMAX_DELAY);

    free(buffer);
}

void Audio::generateTone(uint16_t frequency, uint16_t duration_ms, int16_t *buffer, size_t buffer_size) {
    float sample_rate = 44100.0f;
    float phase_increment = (2.0f * M_PI * frequency) / sample_rate;
    float phase = 0.0f;
    int16_t amplitude = (int16_t)((volume_level / 100.0f) * 32767.0f);

    for (size_t i = 0; i < buffer_size; i++) {
        buffer[i] = (int16_t)(amplitude * sinf(phase));
        phase += phase_increment;
        if (phase > 2.0f * M_PI) {
            phase -= 2.0f * M_PI;
        }
    }
}

void Audio::playSound(const uint8_t *data, size_t size) {
    if (!is_initialized) return;

    size_t bytes_written;
    i2s_write(I2S_NUM, (void *)data, size, &bytes_written, portMAX_DELAY);
}

void Audio::setVolume(uint8_t volume) {
    volume_level = constrain(volume, 0, 100);
}

void Audio::stop() {
    // Flush I2S buffer
    i2s_zero_dma_buffer(I2S_NUM);
}
