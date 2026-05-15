#include "audio.h"
#include "core/Logger.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

#define ES8311_ADDR 0x18

// ── ES8311 I2C helpers ────────────────────────────────────────────────────────

static void es_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    uint8_t err = Wire.endTransmission();
    if (err) Logger::info("Audio", String("I2C REG0x") + String(reg, HEX) + " write err=" + err);
}

static uint8_t es_read(uint8_t reg) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

// ── ES8311 init ───────────────────────────────────────────────────────────────
//
// Register values are aligned with the Waveshare-compatible ESP32-audioI2S
// library (schreibfaul1) which is verified to work on this exact board.
// Target: 48 000 Hz, 16-bit I2S std, MCLK = 12 288 000 Hz (256 × fs)

static void es8311_init(uint8_t vol_pct) {
    // Hard reset
    es_write(0x00, 0x1F);
    delay(30);
    es_write(0x00, 0x00);
    delay(10);

    // ── Clock configuration ────────────────────────────────────────────────
    es_write(0x01, 0x30);   // external MCLK from pin, clock-path enables active
    es_write(0x02, 0x00);
    es_write(0x03, 0x10);   // BCLK div (in slave mode this is informational)
    es_write(0x04, 0x10);   // ADC OSR
    es_write(0x05, 0x10);   // DAC OSR
    es_write(0x06, 0x00);   // LRCK div high
    es_write(0x07, 0xFF);   // LRCK div low → ÷256 → 48 000 Hz
    es_write(0x08, 0xFF);   // (0xFF required — was wrongly set to 0x00)

    // ── Serial format: I²S standard, 16-bit ───────────────────────────────
    es_write(0x09, 0x00);   // ADC: I²S std, 16-bit
    es_write(0x0A, 0x00);   // DAC: I²S std, 16-bit

    // ── ADC section (microphone disabled) ─────────────────────────────────
    es_write(0x0D, 0x01);
    es_write(0x0E, 0x02);
    es_write(0x0F, 0x44);
    es_write(0x10, 0x02);   // ADC analog power
    es_write(0x11, 0x00);
    es_write(0x12, 0x02);   // ADC digital volume
    es_write(0x13, 0x04);   // ADC HPF
    es_write(0x14, 0x1A);
    es_write(0x15, 0x00);
    es_write(0x16, 0x00);
    es_write(0x17, 0xBF);   // ADC volume max

    // ── DAC section ───────────────────────────────────────────────────────
    // REG1A = 0xA0: DAC digital control.  This value is correct; setting it
    // to 0x00 silences the DAC digital path (confirmed against the working
    // Waveshare / ESP32-audioI2S reference).
    es_write(0x1A, 0xA0);
    es_write(0x1B, 0x00);
    es_write(0x1C, 0xF8);
    es_write(0x1D, 0x3C);
    es_write(0x1E, 0x28);
    es_write(0x1F, 0x00);
    es_write(0x20, 0x00);
    es_write(0x21, 0x00);
    es_write(0x22, 0x00);
    es_write(0x23, 0x00);

    // DAC digital unmute before setting volume
    es_write(0x31, 0x00);

    // DAC volume: 0x00 = mute, 0xBF = 0 dB
    uint8_t dac_vol = (uint8_t)((uint32_t)vol_pct * 0xBF / 100);
    es_write(0x32, dac_vol);

    // ── Analog output power ───────────────────────────────────────────────
    es_write(0x37, 0x08);   // headphone driver on
    es_write(0x44, 0x08);   // speaker amplifier on
    es_write(0x45, 0x00);

    delay(150);

    // Readback key registers — if these match what was written, I2C is healthy
    uint8_t r01 = es_read(0x01);
    uint8_t r08 = es_read(0x08);
    uint8_t r1A = es_read(0x1A);
    uint8_t r32 = es_read(0x32);
    Logger::info("Audio", String("ES8311 init:")
        + " REG01=0x" + String(r01, HEX)
        + " REG08=0x" + String(r08, HEX)
        + " REG1A=0x" + String(r1A, HEX)
        + " REG32=0x" + String(r32, HEX)
        + " (expected 30 FF A0 " + String(dac_vol, HEX) + ")");
}

// ── I2S: 48 kHz, 16-bit stereo, MCLK = 256 × fs = 12.288 MHz ────────────────

static bool i2s_audio_init() {
    i2s_config_t cfg     = {};
    cfg.mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate      = 48000;
    cfg.bits_per_sample  = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format   = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_desc_num     = 8;
    cfg.dma_frame_num    = 256;
    cfg.use_apll         = false;   // standard PLL — more reliable on ESP32-S3
    cfg.tx_desc_auto_clear = true;
    cfg.mclk_multiple    = I2S_MCLK_MULTIPLE_256; // MCLK = 256 × 48000 = 12.288 MHz

    esp_err_t r1 = i2s_driver_install((i2s_port_t)I2S_NUM, &cfg, 0, NULL);
    if (r1 != ESP_OK) {
        Logger::info("Audio", String("I2S driver install err=") + r1);
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_MCLK;
    pins.bck_io_num   = I2S_BCLK;
    pins.ws_io_num    = I2S_LRCK;
    pins.data_out_num = I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    esp_err_t r2 = i2s_set_pin((i2s_port_t)I2S_NUM, &pins);
    if (r2 != ESP_OK) {
        Logger::info("Audio", String("I2S set_pin err=") + r2);
        return false;
    }

    i2s_zero_dma_buffer((i2s_port_t)I2S_NUM);
    Logger::info("Audio", String("I2S OK: 48kHz 16bit stereo")
        + " MCLK=GPIO" + I2S_MCLK
        + " BCLK=GPIO" + I2S_BCLK
        + " LRCK=GPIO" + I2S_LRCK
        + " DOUT=GPIO" + I2S_DOUT);
    return true;
}

// ── Tone synthesis task ───────────────────────────────────────────────────────

struct ToneCmd { uint16_t freq; uint16_t ms; };

static constexpr uint32_t SR    = 48000;
static constexpr size_t   CHUNK = 256;  // matches dma_frame_num
static constexpr int      RAMP  = 240;  // 5 ms soft attack at 48 kHz

void Audio::toneTask(void *param) {
    auto    *self = static_cast<Audio *>(param);
    int16_t  buf[CHUNK * 2];
    ToneCmd  cmd;

    Logger::info("Audio", "Tone task gestartet, warte auf Befehle...");

    for (;;) {
        if (xQueueReceive(self->_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (cmd.freq == 0) continue;

        Logger::info("Audio", String("Tone: ") + cmd.freq + " Hz " + cmd.ms + " ms");

        uint32_t total  = (uint32_t)SR * cmd.ms / 1000;
        float    step   = 2.0f * M_PI * (float)cmd.freq / (float)SR;
        float    phase  = 0.0f;
        float    amp    = 32767.0f * (float)self->volume_level / 100.0f;
        int      ramp_n = 0;

        while (total > 0) {
            size_t n = (total < CHUNK) ? (size_t)total : CHUNK;
            for (size_t i = 0; i < n; i++) {
                float   env = (ramp_n < RAMP) ? (float)ramp_n / (float)RAMP : 1.0f;
                if (ramp_n < RAMP) ramp_n++;
                int16_t s       = (int16_t)(sinf(phase) * amp * env);
                buf[i * 2]      = s;
                buf[i * 2 + 1]  = s;
                phase += step;
                if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
            }
            size_t    written = 0;
            esp_err_t err = i2s_write((i2s_port_t)I2S_NUM,
                                      buf, n * 4, &written, pdMS_TO_TICKS(500));
            if (err != ESP_OK)
                Logger::info("Audio", String("i2s_write err=") + err);
            total -= (uint32_t)n;
        }
        // Silence flush to prevent click at tone end
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

    // Ensure I2C is ready (i2c_bus.begin() in App.cpp configures port 0 with
    // the same pins, but re-calling is safe and guards against ordering changes)
    Wire.begin(TOUCH_SDA, TOUCH_SCL, I2C_FREQ);

    Wire.beginTransmission(ES8311_ADDR);
    uint8_t i2c_err = Wire.endTransmission();
    Logger::info("Audio", String("ES8311 I2C 0x18 → ")
        + (i2c_err == 0 ? "OK" : String("FEHLER code=") + i2c_err));
    if (i2c_err != 0) {
        Logger::info("Audio", "ES8311 nicht erreichbar – Audio deaktiviert");
        return;
    }

    // ① I2S first — starts MCLK/BCLK/LRCK so they are stable when the
    //   codec latches its registers (some ES8311 regs are clocked by MCLK)
    if (!i2s_audio_init()) {
        Logger::info("Audio", "I2S fehlgeschlagen – Audio deaktiviert");
        return;
    }
    delay(20);

    // ② Configure ES8311 with clocks running
    es8311_init(volume_level);

    is_initialized = true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        toneTask, "audio_tone", 4096, this, 5, nullptr, 1);
    Logger::info("Audio", String("Audio bereit – Task=") + (ok == pdPASS ? "OK" : "FEHLER")
        + " vol=" + volume_level + "% 48kHz 16bit");

    // Startup beep — audible confirmation the pipeline is alive
    playTone(1000, 250);
}

void Audio::playTone(uint16_t frequency, uint16_t duration_ms) {
    if (!is_initialized) {
        Logger::info("Audio", "playTone: nicht initialisiert!");
        return;
    }
    if (!_queue) return;
    if (uxQueueSpacesAvailable(_queue) == 0) {
        Logger::info("Audio", "playTone: Queue voll – Ton verworfen");
        return;
    }
    ToneCmd cmd{ frequency, duration_ms };
    xQueueSend(_queue, &cmd, 0);
}

void Audio::setVolume(uint8_t volume) {
    volume_level = (volume > 100) ? 100 : volume;
    if (is_initialized) {
        uint8_t dac_vol = (uint8_t)((uint32_t)volume_level * 0xBF / 100);
        es_write(0x32, dac_vol);
        Logger::info("Audio", String("Volume → ") + volume_level
            + "% REG32=0x" + String(dac_vol, HEX));
    }
}

void Audio::playSound(const uint8_t * /*data*/, size_t /*size*/) {}
void Audio::stop() { if (_queue) xQueueReset(_queue); }
