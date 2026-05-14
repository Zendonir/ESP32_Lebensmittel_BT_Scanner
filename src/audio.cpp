#include "audio.h"
#include "core/Logger.h"
#include <Wire.h>
#include <driver/i2s_std.h>
#include <math.h>

// ── ES8311 I2C (Adresse 0x18, gleicher Bus wie Touch/TCA9554) ────────────────

#define ES8311_ADDR 0x18

static void es_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

// 44 100 Hz, MCLK = 256×44100 = 11 289 600 Hz
// BCLK = MCLK/4 = 2 822 400 = 64×44100  (32-Bit-Slots, Stereo)
// LRCK = MCLK/256 = 44 100 Hz
static void es8311_init(uint8_t vol_pct) {
    es_write(0x00, 0x1F);   // Reset
    delay(20);
    es_write(0x00, 0x00);   // Release

    // Clock manager
    es_write(0x01, 0x80);   // MCLK von MCLK-Pin, kein Invert, Pre÷1
    es_write(0x02, 0x00);   // ADC-Takt-Div = 0
    es_write(0x03, 0x04);   // BCLK_DIV = 4  → MCLK/4 = 2 822 400 Hz
    es_write(0x04, 0x10);   // ADC OSR
    es_write(0x05, 0x10);   // DAC OSR
    es_write(0x06, 0x00);   // LRCK High-Nibble = 0
    es_write(0x07, 0xFF);   // LRCK Low = 255 → Divisor 256 → 44 100 Hz ✓
    es_write(0x08, 0x00);

    // Serielles Datenformat: I²S Standard, 32-Bit (passt zu 32-Bit-I2S-Slots)
    es_write(0x09, 0x04);

    // ADC-Abschnitt (Mikrofon deaktiviert)
    es_write(0x0D, 0x01);
    es_write(0x0E, 0x00);
    es_write(0x0F, 0x44);
    es_write(0x11, 0x00);   // ADC unmuted
    es_write(0x16, 0x00);   // ADC-Lautstärke 0 dB
    es_write(0x17, 0xBF);

    // DAC
    es_write(0x1A, 0xA0);   // DAC digital power on
    es_write(0x1B, 0x00);
    es_write(0x1C, 0xF8);
    es_write(0x1D, 0x3C);
    es_write(0x1E, 0x28);
    es_write(0x1F, 0x00);

    // DAC-Lautstärke: 0xBF = 0 dB, 0x00 = Mute
    // Skalierung: pct 0–100 → 0x00–0xBF
    uint8_t dac_vol = (uint8_t)((uint32_t)vol_pct * 0xBF / 100);
    es_write(0x32, dac_vol);

    // Analoger Ausgang
    es_write(0x37, 0x08);   // Headphone-Treiber ein
    es_write(0x44, 0x08);   // Lautsprecher ein
    es_write(0x45, 0x00);

    delay(100);
}

// ── I2S ──────────────────────────────────────────────────────────────────────

static i2s_chan_handle_t s_tx = nullptr;

static bool i2s_audio_init() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(
        (i2s_port_t)I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    if (i2s_new_channel(&chan_cfg, &s_tx, nullptr) != ESP_OK) return false;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)I2S_MCLK,
            .bclk = (gpio_num_t)I2S_BCLK,
            .ws   = (gpio_num_t)I2S_LRCK,
            .dout = (gpio_num_t)I2S_DOUT,
            .din  = (gpio_num_t)I2S_DIN,
            .invert_flags = { 0 },
        },
    };

    if (i2s_channel_init_std_mode(s_tx, &std_cfg) != ESP_OK) return false;
    return i2s_channel_enable(s_tx) == ESP_OK;
}

// ── Ton-Erzeugung (FreeRTOS-Task) ────────────────────────────────────────────

struct ToneCmd { uint16_t freq; uint16_t ms; };

static constexpr uint32_t SR    = 44100;
static constexpr size_t   CHUNK = 128;   // Frames pro I2S-Schreibaufruf

void Audio::toneTask(void *param) {
    auto *self = static_cast<Audio *>(param);
    int32_t buf[CHUNK * 2];  // 2 Kanäle × 32-Bit
    ToneCmd cmd;

    for (;;) {
        if (xQueueReceive(self->_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (!s_tx || cmd.freq == 0) continue;

        uint32_t total = (uint32_t)SR * cmd.ms / 1000;
        float    step  = 2.0f * M_PI * (float)cmd.freq / (float)SR;
        float    phase = 0.0f;
        float    amp   = (float)0x3FFFFFFF * self->volume_level / 100.0f;

        while (total > 0) {
            size_t n = (total < CHUNK) ? (size_t)total : CHUNK;
            for (size_t i = 0; i < n; i++) {
                int32_t s      = (int32_t)(sinf(phase) * amp);
                buf[i * 2]     = s;
                buf[i * 2 + 1] = s;
                phase += step;
                if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
            }
            size_t written;
            i2s_channel_write(s_tx, buf, n * 8, &written, pdMS_TO_TICKS(500));
            total -= (uint32_t)n;
        }
        // Kurze Stille zum sauberen Ausklingen
        memset(buf, 0, CHUNK * 8);
        size_t w;
        i2s_channel_write(s_tx, buf, CHUNK * 8, &w, pdMS_TO_TICKS(100));
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

Audio audio_obj;

Audio::Audio() : volume_level(70), is_initialized(false), _queue(nullptr) {}

void Audio::init() {
    _queue = xQueueCreate(8, sizeof(ToneCmd));

    // I2C-Probe: prüfe ob ES8311 auf Adresse 0x18 antwortet
    Wire.beginTransmission(ES8311_ADDR);
    uint8_t i2c_err = Wire.endTransmission();
    Logger::info("Audio", String("ES8311 I2C probe 0x18 → ") +
        (i2c_err == 0 ? "OK" : String("FEHLER code=") + i2c_err));
    if (i2c_err != 0) {
        Logger::info("Audio", "ES8311 nicht gefunden – Audio deaktiviert");
        return;
    }

    if (!i2s_audio_init()) {
        Logger::info("Audio", "I2S init fehlgeschlagen");
        return;
    }
    Logger::info("Audio", String("I2S bereit MCLK=GPIO") + I2S_MCLK
        + " BCLK=GPIO" + I2S_BCLK + " LRCK=GPIO" + I2S_LRCK
        + " DOUT=GPIO" + I2S_DOUT);

    delay(100);
    es8311_init(volume_level);

    is_initialized = true;
    BaseType_t ok = xTaskCreatePinnedToCore(toneTask, "audio_tone", 4096, this, 5, nullptr, 1);
    Logger::info("Audio", String("ES8311 bereit, Task=") + (ok == pdPASS ? "OK" : "FEHLER")
        + " vol=" + volume_level + "%");
}

void Audio::playTone(uint16_t frequency, uint16_t duration_ms) {
    if (!is_initialized || !_queue) return;
    ToneCmd cmd{ frequency, duration_ms };
    xQueueSend(_queue, &cmd, 0);  // nicht blockieren
}

void Audio::playSound(const uint8_t * /*data*/, size_t /*size*/) {
    // Nicht implementiert (kein SD-Karten-Playback benötigt)
}

void Audio::setVolume(uint8_t volume) {
    volume_level = (volume > 100) ? 100 : volume;
    if (is_initialized) {
        uint8_t dac_vol = (uint8_t)((uint32_t)volume_level * 0xBF / 100);
        es_write(0x32, dac_vol);
    }
}

void Audio::stop() {
    if (_queue) xQueueReset(_queue);
}
