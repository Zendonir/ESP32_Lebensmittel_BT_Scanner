#include "audio.h"
#include "core/Logger.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

#define ES8311_ADDR 0x18

static void es_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    uint8_t err = Wire.endTransmission();
    if (err) Logger::info("Audio", String("I2C write REG") + reg + " err=" + err);
}

static uint8_t es_read(uint8_t reg) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

// 48 000 Hz, MCLK = 256 × 48000 = 12 288 000 Hz
// BCLK = 32-bit stereo → 48000 × 2 × 32 = 3 072 000 Hz = MCLK / 4
// LRCK = MCLK / 256 = 48 000 Hz
static void es8311_init(uint8_t vol_pct) {
    // Hard reset
    es_write(0x00, 0x1F);
    delay(30);
    es_write(0x00, 0x00);
    delay(10);

    // Clock manager: external MCLK from MCLK pin, enable clocks, no pre-division
    es_write(0x01, 0x30);   // bit7=0: ext MCLK, bits5:4=11: clock enables, bits3:0=0: div=1
    es_write(0x02, 0x00);   // ADC CLK pre-div = 1
    es_write(0x03, 0x04);   // BCLK_DIV = 4 → MCLK/4 = 3 072 000 Hz
    es_write(0x04, 0x10);   // ADC OSR = 16 (256/16 = 16)
    es_write(0x05, 0x10);   // DAC OSR = 16
    es_write(0x06, 0x00);   // LRCK_DIV high byte = 0
    es_write(0x07, 0xFF);   // LRCK_DIV low byte = 255 → divisor 256 → 48 000 Hz
    es_write(0x08, 0x00);

    // Serial port: I²S standard, 32-bit word
    es_write(0x09, 0x00);   // ADC interface: I²S std, 32-bit (0x04)? Try 0x00 = 24bit first
    es_write(0x0A, 0x00);   // DAC interface: I²S std, 32-bit

    // ADC section (mic disabled)
    es_write(0x0D, 0x01);
    es_write(0x0E, 0x02);   // ADC digital enable
    es_write(0x0F, 0x44);
    es_write(0x11, 0x00);
    es_write(0x12, 0x00);
    es_write(0x13, 0x10);
    es_write(0x14, 0x1A);
    es_write(0x16, 0x00);
    es_write(0x17, 0xBF);   // ADC volume max

    // DAC
    es_write(0x1A, 0xA0);   // DAC digital power on + mono mix
    es_write(0x1B, 0x00);
    es_write(0x1C, 0xF8);   // DAC output mixer
    es_write(0x1D, 0x3C);
    es_write(0x1E, 0x28);
    es_write(0x1F, 0x00);

    // DAC volume: 0xBF = 0 dB max, 0x00 = mute; scale pct → 0x00..0xBF
    uint8_t dac_vol = (uint8_t)((uint32_t)vol_pct * 0xBF / 100);
    es_write(0x32, dac_vol);

    // Analog output power on
    es_write(0x37, 0x08);   // headphone amp on
    es_write(0x44, 0x08);   // speaker amp on
    es_write(0x45, 0x00);

    delay(150);

    // Verify chip ID register (REG0D should return 0x01 or similar)
    uint8_t id = es_read(0x0D);
    Logger::info("Audio", String("ES8311 REG0D=0x") + String(id, HEX)
        + " dac_vol=0x" + String(dac_vol, HEX));
}

// ── I2S (legacy driver) ───────────────────────────────────────────────────────

static bool i2s_audio_init() {
    i2s_config_t cfg = {};
    cfg.mode               = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate        = 48000;
    cfg.bits_per_sample    = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format     = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags   = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count      = 8;
    cfg.dma_buf_len        = 128;
    cfg.use_apll           = false;
    cfg.tx_desc_auto_clear = true;
    cfg.mclk_multiple      = I2S_MCLK_MULTIPLE_256;

    esp_err_t r1 = i2s_driver_install((i2s_port_t)I2S_NUM, &cfg, 0, NULL);
    if (r1 != ESP_OK) {
        Logger::info("Audio", String("I2S driver install fehlgeschlagen err=") + r1);
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num       = I2S_MCLK;
    pins.bck_io_num       = I2S_BCLK;
    pins.ws_io_num        = I2S_LRCK;
    pins.data_out_num     = I2S_DOUT;
    pins.data_in_num      = I2S_PIN_NO_CHANGE;

    esp_err_t r2 = i2s_set_pin((i2s_port_t)I2S_NUM, &pins);
    if (r2 != ESP_OK) {
        Logger::info("Audio", String("I2S set_pin fehlgeschlagen err=") + r2);
        return false;
    }

    i2s_zero_dma_buffer((i2s_port_t)I2S_NUM);
    Logger::info("Audio", String("I2S bereit: 48kHz 16bit stereo MCLK=GPIO") + I2S_MCLK
        + " BCLK=GPIO" + I2S_BCLK + " LRCK=GPIO" + I2S_LRCK + " DOUT=GPIO" + I2S_DOUT);
    return true;
}

// ── Ton-Task ─────────────────────────────────────────────────────────────────

struct ToneCmd { uint16_t freq; uint16_t ms; };

static constexpr uint32_t SR    = 48000;
static constexpr size_t   CHUNK = 128;

void Audio::toneTask(void *param) {
    auto *self = static_cast<Audio *>(param);
    int16_t buf[CHUNK * 2];  // 16-bit stereo
    ToneCmd cmd;

    for (;;) {
        if (xQueueReceive(self->_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (cmd.freq == 0) continue;

        uint32_t total = (uint32_t)SR * cmd.ms / 1000;
        float step  = 2.0f * M_PI * (float)cmd.freq / (float)SR;
        float phase = 0.0f;
        // 16-bit amplitude: 32767 * vol/100
        float amp = 32767.0f * self->volume_level / 100.0f;

        while (total > 0) {
            size_t n = (total < CHUNK) ? (size_t)total : CHUNK;
            for (size_t i = 0; i < n; i++) {
                int16_t s       = (int16_t)(sinf(phase) * amp);
                buf[i * 2]      = s;
                buf[i * 2 + 1]  = s;
                phase += step;
                if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
            }
            size_t written = 0;
            i2s_write((i2s_port_t)I2S_NUM, buf, n * 4, &written, pdMS_TO_TICKS(500));
            total -= (uint32_t)n;
        }
        // Short silence to flush DMA
        memset(buf, 0, CHUNK * 4);
        size_t w = 0;
        i2s_write((i2s_port_t)I2S_NUM, buf, CHUNK * 4, &w, pdMS_TO_TICKS(100));
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

Audio audio_obj;

Audio::Audio() : volume_level(70), is_initialized(false), _queue(nullptr) {}

void Audio::init() {
    _queue = xQueueCreate(8, sizeof(ToneCmd));

    Wire.beginTransmission(ES8311_ADDR);
    uint8_t i2c_err = Wire.endTransmission();
    Logger::info("Audio", String("ES8311 I2C 0x18 → ")
        + (i2c_err == 0 ? "OK" : String("FEHLER code=") + i2c_err));
    if (i2c_err != 0) {
        Logger::info("Audio", "ES8311 nicht erreichbar – Audio deaktiviert");
        return;
    }

    // Configure ES8311 via I2C first (register writes work without MCLK running)
    es8311_init(volume_level);

    // Then start I2S master — this begins MCLK/BCLK/LRCK output to the codec
    if (!i2s_audio_init()) return;

    is_initialized = true;
    BaseType_t ok = xTaskCreatePinnedToCore(toneTask, "audio_tone", 4096, this, 5, nullptr, 1);
    Logger::info("Audio", String("ES8311 bereit Task=") + (ok == pdPASS ? "OK" : "FEHLER")
        + " vol=" + volume_level + "%");

    // Play a short startup beep to verify audio is working
    playTone(1000, 150);
}

void Audio::playTone(uint16_t frequency, uint16_t duration_ms) {
    if (!is_initialized || !_queue) return;
    ToneCmd cmd{ frequency, duration_ms };
    xQueueSend(_queue, &cmd, 0);
}

void Audio::setVolume(uint8_t volume) {
    volume_level = (volume > 100) ? 100 : volume;
    if (is_initialized) {
        uint8_t dac_vol = (uint8_t)((uint32_t)volume_level * 0xBF / 100);
        es_write(0x32, dac_vol);
    }
}

void Audio::playSound(const uint8_t * /*data*/, size_t /*size*/) {}

void Audio::stop() {
    if (_queue) xQueueReset(_queue);
}
