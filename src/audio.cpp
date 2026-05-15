#include "audio.h"
#include "core/Logger.h"
#include <Wire.h>
#include <ESP_I2S.h>
#include <math.h>

#define ES8311_ADDR 0x18

// File-scoped I2S instance (new stack: ESP_I2S.h / IDF 5.x)
static ESP_I2S _i2s;

// ── ES8311 I2C helpers ────────────────────────────────────────────────────────

static void es_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    uint8_t err = Wire.endTransmission();
    if (err) Logger::info("Audio", String("I2C REG0x") + String(reg, HEX) + " err=" + err);
}

static uint8_t es_read(uint8_t reg) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

// ── ES8311 codec init ─────────────────────────────────────────────────────────
// Target: 48 000 Hz · 16-bit · I²S standard · MCLK = 256 × fs = 12.288 MHz
// Register values cross-checked against the Waveshare / schreibfaul1 reference.

static void es8311_init(uint8_t vol_pct) {
    // Hard reset
    es_write(0x00, 0x1F);
    delay(30);
    es_write(0x00, 0x00);
    delay(10);

    // ── Clock ──────────────────────────────────────────────────────────────────
    es_write(0x01, 0x30);   // external MCLK pin; clock-path enables active
    es_write(0x02, 0x00);
    es_write(0x03, 0x10);   // BCLK divider (informational in slave mode)
    es_write(0x04, 0x10);   // ADC OSR
    es_write(0x05, 0x10);   // DAC OSR
    es_write(0x06, 0x00);   // LRCK div high byte
    es_write(0x07, 0xFF);   // LRCK div low  → ÷256 → 48 000 Hz
    es_write(0x08, 0xFF);   // required; 0x00 was confirmed to cause silence

    // ── Serial format: I²S standard, 16-bit ───────────────────────────────────
    es_write(0x09, 0x00);   // ADC: I²S, 16-bit
    es_write(0x0A, 0x00);   // DAC: I²S, 16-bit

    // ── ADC (microphone disabled) ──────────────────────────────────────────────
    es_write(0x0D, 0x01);
    es_write(0x0E, 0x02);
    es_write(0x0F, 0x44);
    es_write(0x10, 0x02);
    es_write(0x11, 0x00);
    es_write(0x12, 0x02);
    es_write(0x13, 0x04);
    es_write(0x14, 0x1A);
    es_write(0x15, 0x00);
    es_write(0x16, 0x00);
    es_write(0x17, 0xBF);

    // ── DAC ───────────────────────────────────────────────────────────────────
    // REG1A = 0xA0 is mandatory; 0x00 silences the DAC digital path entirely.
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

    es_write(0x31, 0x00);   // DAC digital unmute

    uint8_t dac_vol = (uint8_t)((uint32_t)vol_pct * 0xBF / 100);
    es_write(0x32, dac_vol);

    // ── Analog output power ────────────────────────────────────────────────────
    es_write(0x37, 0x08);   // headphone driver on
    es_write(0x44, 0x08);   // speaker amplifier on
    es_write(0x45, 0x00);

    delay(150);

    // Readback for diagnostics
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

// ── I2S init (ESP_I2S.h new stack) ───────────────────────────────────────────

static bool i2s_audio_init() {
    // Pin assignment: BCLK, LRCK (WS), DOUT (data to codec), no mic, MCLK
    _i2s.setPins(I2S_BCLK, I2S_LRCK, I2S_DOUT, /*din=*/-1, I2S_MCLK);

    // MCLK = 256 × 48000 = 12.288 MHz (ES8311 requires a stable MCLK)
    _i2s.setMCLKMultiplier(256);

    bool ok = _i2s.begin(I2S_MODE_STD, 48000,
                         I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    if (!ok) {
        Logger::info("Audio", "I2S begin() failed");
        return false;
    }

    Logger::info("Audio", String("I2S OK: 48kHz 16bit stereo")
        + "  MCLK=GPIO" + I2S_MCLK
        + "  BCLK=GPIO" + I2S_BCLK
        + "  LRCK=GPIO" + I2S_LRCK
        + "  DOUT=GPIO" + I2S_DOUT);
    return true;
}

// ── Tone synthesis task ───────────────────────────────────────────────────────

struct ToneCmd { uint16_t freq; uint16_t ms; };

static constexpr uint32_t SR    = 48000;
static constexpr size_t   CHUNK = 256;  // frames per DMA write
static constexpr int      RAMP  = 240;  // ~5 ms attack ramp at 48 kHz

// Write a silence block to flush residual signal (prevents click at tone end)
static void flush_silence() {
    static int16_t silence[CHUNK * 2] = {};
    _i2s.write((uint8_t *)silence, sizeof(silence), /*blocking=*/true);
}

void Audio::toneTask(void *param) {
    auto    *self = static_cast<Audio *>(param);
    int16_t  buf[CHUNK * 2];
    ToneCmd  cmd;

    Logger::info("Audio", "Tone task gestartet");

    for (;;) {
        if (xQueueReceive(self->_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;

        if (cmd.freq == 0) {
            // Treat freq=0 as a timed silence (used for pauses between beeps)
            uint32_t frames = (uint32_t)SR * cmd.ms / 1000;
            static int16_t zeros[CHUNK * 2] = {};
            while (frames > 0) {
                size_t n = (frames < CHUNK) ? (size_t)frames : CHUNK;
                _i2s.write((uint8_t *)zeros, n * 4, /*blocking=*/true);
                frames -= (uint32_t)n;
            }
            continue;
        }

        Logger::info("Audio", String("Tone: ") + cmd.freq + " Hz  " + cmd.ms + " ms");

        uint32_t total  = (uint32_t)SR * cmd.ms / 1000;
        float    step   = 2.0f * M_PI * (float)cmd.freq / (float)SR;
        float    phase  = 0.0f;
        float    amp    = 32767.0f * (float)self->volume_level / 100.0f;
        int      ramp_n = 0;

        while (total > 0) {
            size_t n = (total < CHUNK) ? (size_t)total : CHUNK;
            for (size_t i = 0; i < n; i++) {
                // Soft attack ramp to avoid click at tone start
                float   env = (ramp_n < RAMP) ? (float)ramp_n / (float)RAMP : 1.0f;
                if (ramp_n < RAMP) ramp_n++;
                int16_t s      = (int16_t)(sinf(phase) * amp * env);
                buf[i * 2]     = s;
                buf[i * 2 + 1] = s;
                phase += step;
                if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
            }
            // Zero-pad tail of last partial chunk
            for (size_t i = n; i < CHUNK; i++) buf[i * 2] = buf[i * 2 + 1] = 0;

            _i2s.write((uint8_t *)buf, CHUNK * 4, /*blocking=*/true);
            total -= (uint32_t)n;
        }

        flush_silence(); // prevent click at tone end
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

Audio audio_obj;
Audio::Audio() : volume_level(70), is_initialized(false), _queue(nullptr) {}

void Audio::init() {
    _queue = xQueueCreate(16, sizeof(ToneCmd));

    // I2C must be up (also initialised by App::initI2C; re-calling is safe)
    Wire.begin(TOUCH_SDA, TOUCH_SCL, I2C_FREQ);

    Wire.beginTransmission(ES8311_ADDR);
    uint8_t i2c_err = Wire.endTransmission();
    Logger::info("Audio", String("ES8311 I2C 0x18 → ")
        + (i2c_err == 0 ? "OK" : String("FEHLER code=") + i2c_err));
    if (i2c_err != 0) {
        Logger::info("Audio", "ES8311 nicht erreichbar – Audio deaktiviert");
        return;
    }

    // Start I2S first so MCLK/BCLK/LRCK are stable when ES8311 latches its regs
    if (!i2s_audio_init()) {
        Logger::info("Audio", "I2S fehlgeschlagen – Audio deaktiviert");
        return;
    }
    delay(20);

    es8311_init(volume_level);
    is_initialized = true;

    BaseType_t ok = xTaskCreatePinnedToCore(
        toneTask, "audio_tone", 4096, this, 5, nullptr, 1);
    Logger::info("Audio", String("Audio bereit – Task=") + (ok == pdPASS ? "OK" : "FEHLER")
        + "  vol=" + volume_level + "%  48kHz 16bit");

    playStartupTone();
}

void Audio::playTone(uint16_t frequency, uint16_t duration_ms) {
    if (!is_initialized) {
        Logger::info("Audio", "playTone: nicht initialisiert!");
        return;
    }
    if (!_queue) return;
    if (uxQueueSpacesAvailable(_queue) == 0) {
        Logger::info("Audio", "playTone: Queue voll");
        return;
    }
    ToneCmd cmd{ frequency, duration_ms };
    xQueueSend(_queue, &cmd, 0);
}

// ── Convenience signals ───────────────────────────────────────────────────────

void Audio::playSuccessTone() {
    playTone(880,  80);
    playTone(1320, 130);
}

void Audio::playErrorTone() {
    playTone(320, 120);
    playTone(200, 280);
}

void Audio::playWarningTone() {
    playTone(880, 80);
    playTone(0,   60);  // 60 ms silence pause
    playTone(880, 80);
}

void Audio::playStartupTone() {
    playTone(523, 80);
    playTone(659, 80);
    playTone(784, 130);
}

void Audio::stopTone() {
    if (_queue) xQueueReset(_queue);
}

// ── Volume ────────────────────────────────────────────────────────────────────

void Audio::setVolume(uint8_t volume) {
    volume_level = (volume > 100) ? 100 : volume;
    if (is_initialized) {
        uint8_t dac_vol = (uint8_t)((uint32_t)volume_level * 0xBF / 100);
        es_write(0x32, dac_vol);
        Logger::info("Audio", String("Volume → ") + volume_level
            + "%  REG32=0x" + String(dac_vol, HEX));
    }
}

void Audio::playSound(const uint8_t * /*data*/, size_t /*size*/) {}
void Audio::stop() { stopTone(); }
