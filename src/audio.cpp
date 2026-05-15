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

// ── ES8311 Codec Init ─────────────────────────────────────────────────────────
//
// Configuration: 48 000 Hz, 16-bit stereo, MCLK = 12 288 000 Hz (256×fs)
//   BCLK = 48000 × 2 × 16 = 1 536 000 Hz  →  MCLK / 8  → REG03 = 0x08
//   LRCK = MCLK / 256 = 48 000 Hz          →  REG06/07 = 0x00/0xFF

static void es8311_init(uint8_t vol_pct) {
    // Hard reset
    es_write(0x00, 0x1F);
    delay(30);
    es_write(0x00, 0x00);
    delay(10);

    // ── Clock manager ──────────────────────────────────────────────────────
    // REG01: bit7=0 external MCLK pin, bits[5:4]=11 clock-path enables, bits[3:0]=0 no pre-div
    es_write(0x01, 0x30);
    es_write(0x02, 0x00);   // ADC pre-div = 1
    es_write(0x03, 0x08);   // BCLK_DIV = 8 → 12.288 MHz / 8 = 1.536 MHz (16-bit stereo)
    es_write(0x04, 0x10);   // ADC OSR
    es_write(0x05, 0x10);   // DAC OSR
    es_write(0x06, 0x00);   // LRCK_DIV high byte
    es_write(0x07, 0xFF);   // LRCK_DIV low byte → divisor 256 → 48 000 Hz
    es_write(0x08, 0x00);

    // ── Serial port format: I²S standard, 16-bit ──────────────────────────
    // REG09/0A [5:4]=00 I²S standard, [2:0]=000 16-bit word length → 0x00
    es_write(0x09, 0x00);   // ADC serial: I²S std, 16-bit
    es_write(0x0A, 0x00);   // DAC serial: I²S std, 16-bit

    // ── ADC section (microphone disabled) ─────────────────────────────────
    es_write(0x0D, 0x01);
    es_write(0x0E, 0x02);
    es_write(0x0F, 0x44);
    es_write(0x11, 0x00);
    es_write(0x12, 0x00);
    es_write(0x13, 0x10);
    es_write(0x14, 0x1A);
    es_write(0x16, 0x00);
    es_write(0x17, 0xBF);

    // ── DAC — stereo mode, no mono mix ────────────────────────────────────
    es_write(0x1A, 0x00);   // DAC control: stereo (was 0xA0 which enabled mono-mix)
    es_write(0x1B, 0x00);
    es_write(0x1C, 0xF8);
    es_write(0x1D, 0x3C);
    es_write(0x1E, 0x28);
    es_write(0x1F, 0x00);

    // ── DAC unmute and volume ──────────────────────────────────────────────
    es_write(0x31, 0x00);   // DAC digital: unmute (prevents codec staying silent after init)
    uint8_t dac_vol = (uint8_t)((uint32_t)vol_pct * 0xBF / 100); // 0x00=mute, 0xBF=0dB
    es_write(0x32, dac_vol);

    // ── Analog output ─────────────────────────────────────────────────────
    es_write(0x37, 0x08);   // headphone driver on
    es_write(0x44, 0x08);   // speaker amplifier on
    es_write(0x45, 0x00);

    delay(150);

    // Readback key registers for diagnostics
    uint8_t r01 = es_read(0x01);
    uint8_t r03 = es_read(0x03);
    uint8_t r09 = es_read(0x09);
    uint8_t r32 = es_read(0x32);
    Logger::info("Audio", String("ES8311 init done")
        + " REG01=0x" + String(r01, HEX)
        + " REG03=0x" + String(r03, HEX)
        + " REG09=0x" + String(r09, HEX)
        + " REG32=0x" + String(r32, HEX)
        + " (vol=" + vol_pct + "% dac=0x" + String(dac_vol, HEX) + ")");
}

// ── I2S legacy driver: 48 kHz, 16-bit stereo, APLL, fixed 12.288 MHz MCLK ───

static bool i2s_audio_init() {
    i2s_config_t cfg = {};
    cfg.mode                = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate         = 48000;
    cfg.bits_per_sample     = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format      = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags    = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_desc_num        = 8;          // buffers in DMA ring (was dma_buf_count)
    cfg.dma_frame_num       = 256;        // frames per buffer   (was dma_buf_len)
    cfg.use_apll            = true;       // APLL for clean audio-grade clock
    cfg.tx_desc_auto_clear  = true;
    cfg.fixed_mclk          = 12288000;   // exact 12.288 MHz = 256 × 48 kHz
    cfg.mclk_multiple       = I2S_MCLK_MULTIPLE_256;

    esp_err_t r1 = i2s_driver_install((i2s_port_t)I2S_NUM, &cfg, 0, NULL);
    if (r1 != ESP_OK) {
        Logger::info("Audio", String("I2S install err=") + r1);
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_MCLK;
    pins.bck_io_num   = I2S_BCLK;
    pins.ws_io_num    = I2S_LRCK;
    pins.data_out_num = I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE; // TX-only, no mic input needed

    esp_err_t r2 = i2s_set_pin((i2s_port_t)I2S_NUM, &pins);
    if (r2 != ESP_OK) {
        Logger::info("Audio", String("I2S set_pin err=") + r2);
        return false;
    }

    i2s_zero_dma_buffer((i2s_port_t)I2S_NUM);

    Logger::info("Audio", String("I2S OK: 48kHz 16bit stereo APLL 12.288MHz")
        + " MCLK=GPIO" + I2S_MCLK
        + " BCLK=GPIO" + I2S_BCLK
        + " LRCK=GPIO" + I2S_LRCK
        + " DOUT=GPIO" + I2S_DOUT);
    return true;
}

// ── Tone synthesis task ───────────────────────────────────────────────────────

struct ToneCmd { uint16_t freq; uint16_t ms; };

static constexpr uint32_t SR    = 48000;
static constexpr size_t   CHUNK = 256;  // matches dma_frame_num → one write per DMA buffer
static constexpr int      RAMP  = 240;  // soft attack: 240 samples ≈ 5 ms at 48 kHz

void Audio::toneTask(void *param) {
    auto    *self = static_cast<Audio *>(param);
    int16_t  buf[CHUNK * 2];  // L + R, 16-bit per channel
    ToneCmd  cmd;

    for (;;) {
        if (xQueueReceive(self->_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;
        if (cmd.freq == 0) continue;

        uint32_t total  = (uint32_t)SR * cmd.ms / 1000;
        float    step   = 2.0f * M_PI * (float)cmd.freq / (float)SR;
        float    phase  = 0.0f;
        float    amp    = 32767.0f * (float)self->volume_level / 100.0f;
        int      ramp_n = 0;

        while (total > 0) {
            size_t n = (total < CHUNK) ? (size_t)total : CHUNK;
            for (size_t i = 0; i < n; i++) {
                // Linear attack envelope to avoid click at tone start
                float env = (ramp_n < RAMP) ? (float)ramp_n / (float)RAMP : 1.0f;
                if (ramp_n < RAMP) ramp_n++;

                int16_t s      = (int16_t)(sinf(phase) * amp * env);
                buf[i * 2]     = s;  // Left
                buf[i * 2 + 1] = s;  // Right
                phase += step;
                if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
            }
            size_t    written = 0;
            esp_err_t err = i2s_write((i2s_port_t)I2S_NUM,
                                      buf, n * 4,           // n frames × 2ch × 2bytes
                                      &written, pdMS_TO_TICKS(500));
            if (err != ESP_OK)
                Logger::info("Audio", String("i2s_write err=") + err);
            else if (written != n * 4)
                Logger::info("Audio", String("i2s_write short ") + written + "/" + (n * 4));

            total -= (uint32_t)n;
        }

        // Flush DMA with silence to prevent click at tone end
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

    // Ensure I2C bus is ready (i2c_bus.begin() in App.cpp configures the same port 0,
    // but call Wire.begin here as an explicit safety net for standalone use)
    Wire.begin(TOUCH_SDA, TOUCH_SCL, I2C_FREQ);

    // Probe ES8311
    Wire.beginTransmission(ES8311_ADDR);
    uint8_t i2c_err = Wire.endTransmission();
    Logger::info("Audio", String("ES8311 I2C 0x18 probe → ")
        + (i2c_err == 0 ? "OK" : String("FEHLER code=") + i2c_err));
    if (i2c_err != 0) {
        Logger::info("Audio", "ES8311 nicht erreichbar – Audio deaktiviert");
        return;
    }

    // ① Start I2S master first so MCLK/BCLK/LRCK are already flowing
    //   when ES8311 registers are written.  Some ES8311 registers are
    //   latched on the first MCLK edge — stable clocks before init matters.
    if (!i2s_audio_init()) {
        Logger::info("Audio", "I2S init fehlgeschlagen – Audio deaktiviert");
        return;
    }
    delay(20); // brief stabilisation before writing codec registers

    // ② Configure ES8311 codec with clocks already running
    es8311_init(volume_level);

    is_initialized = true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        toneTask, "audio_tone", 4096, this, 5, nullptr, 1);
    Logger::info("Audio", String("Audio bereit – Task=") + (ok == pdPASS ? "OK" : "FEHLER")
        + " SR=48kHz 16bit stereo APLL vol=" + volume_level + "%");

    // Startup beep: short 1 kHz tone verifies the full pipeline is alive
    playTone(1000, 200);
}

void Audio::playTone(uint16_t frequency, uint16_t duration_ms) {
    if (!is_initialized || !_queue) return;
    if (uxQueueSpacesAvailable(_queue) == 0) {
        Logger::info("Audio", "Tone queue voll – Ton verworfen");
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

void Audio::stop() {
    if (_queue) xQueueReset(_queue);
}
