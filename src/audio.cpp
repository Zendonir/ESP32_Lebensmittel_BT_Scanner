#include "audio.h"
#include "core/Logger.h"
#include <driver/i2s.h>
#include <math.h>

#define ES8311_ADDR 0x18

// ── ES8311 I2C helpers ────────────────────────────────────────────────────────

static TwoWire *s_wire = nullptr;

static void es_write(uint8_t reg, uint8_t val) {
    s_wire->beginTransmission(ES8311_ADDR);
    s_wire->write(reg);
    s_wire->write(val);
    uint8_t err = s_wire->endTransmission();
    if (err) Logger::info("Audio", String("I2C REG0x") + String(reg, HEX) + " err=" + err);
}

static uint8_t es_read(uint8_t reg) {
    s_wire->beginTransmission(ES8311_ADDR);
    s_wire->write(reg);
    s_wire->endTransmission(false);
    s_wire->requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
    return s_wire->available() ? s_wire->read() : 0xFF;
}

// ── ES8311 codec init ─────────────────────────────────────────────────────────
// Target: 48 000 Hz · 16-bit · I²S standard · MCLK = 256 × fs = 12.288 MHz
//
// REG00 Bit7 = CSM_ON: MUST be 1 after reset or the codec state machine stays
// off and the chip produces no output regardless of other settings.
//
// REG09/0A Bits[3:2] = WL (word length): 00=24-bit, 11=16-bit.
// ESP32 sends 16-bit, so WL must be 11 → 0x0C. Leaving 0x00 (24-bit) creates
// a framing mismatch and the DAC never sees valid samples.

static void es8311_init(uint8_t vol_pct) {
    // ── Reset ─────────────────────────────────────────────────────────────────
    es_write(0x00, 0x1F);   // assert all resets
    delay(30);
    es_write(0x00, 0x80);   // CSM_ON=1: activate internal state machine
    delay(10);

    // ── Clock manager ─────────────────────────────────────────────────────────
    es_write(0x01, 0x30);   // external MCLK; enable all clock paths
    es_write(0x02, 0x00);   // MULT=1
    es_write(0x03, 0x10);   // BCLK divider
    es_write(0x04, 0x10);   // ADC OSR = 32
    es_write(0x05, 0x00);
    es_write(0x06, 0x00);   // LRCK div high (irrelevant in slave mode)
    es_write(0x07, 0x00);   // LRCK div low  (irrelevant in slave mode)
    es_write(0x08, 0xFF);   // clock control — 0x00 silences output

    // ── Serial format: I²S, 16-bit ────────────────────────────────────────────
    // Bits [3:2] = WL: 00=24-bit, 01=20-bit, 10=18-bit, 11=16-bit
    // 0x0C = 0b00001100 → FMT=I²S standard, WL=16-bit
    es_write(0x09, 0x0C);   // ADC (recording) — must match even if ADC unused
    es_write(0x0A, 0x0C);   // DAC (playback)

    // ── System / analog power ─────────────────────────────────────────────────
    es_write(0x0D, 0x01);
    es_write(0x0E, 0x02);
    es_write(0x0F, 0x44);
    es_write(0x10, 0x1C);   // HP output gain
    es_write(0x11, 0x00);
    es_write(0x12, 0x00);   // DAC input = I²S path
    es_write(0x13, 0x10);   // analog bias
    es_write(0x14, 0x1A);
    es_write(0x15, 0x00);
    es_write(0x16, 0x00);
    es_write(0x17, 0xBF);

    // ── DAC digital path ──────────────────────────────────────────────────────
    // REG1A must be 0xA0; 0x00 disables the DAC digital path entirely.
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

    // ── DAC control ───────────────────────────────────────────────────────────
    es_write(0x31, 0x00);   // unmute
    uint8_t dac_vol = (uint8_t)((uint32_t)vol_pct * 0xBF / 100);
    es_write(0x32, dac_vol);
    es_write(0x37, 0x08);

    // ── Analog output power ───────────────────────────────────────────────────
    es_write(0x45, 0x00);   // GP register — must be 0x00

    delay(150);

    // ── Verify key registers ──────────────────────────────────────────────────
    uint8_t r00 = es_read(0x00);
    uint8_t r01 = es_read(0x01);
    uint8_t r08 = es_read(0x08);
    uint8_t r09 = es_read(0x09);
    uint8_t r1A = es_read(0x1A);
    uint8_t r32 = es_read(0x32);
    Logger::info("Audio", String("ES8311 regs:")
        + " R00=0x" + String(r00, HEX) + "(exp 80)"
        + " R01=0x" + String(r01, HEX) + "(exp 30)"
        + " R08=0x" + String(r08, HEX) + "(exp FF)"
        + " R09=0x" + String(r09, HEX) + "(exp 0C)"
        + " R1A=0x" + String(r1A, HEX) + "(exp A0)"
        + " R32=0x" + String(r32, HEX) + "(exp " + String(dac_vol, HEX) + ")");
}

// ── I2S: 48 kHz, 16-bit stereo ───────────────────────────────────────────────
// use_apll=true + fixed_mclk=12288000: forces APLL to generate a stable
// 12.288 MHz MCLK on GPIO12. Without this, the clock divider rounding on
// the base 80 MHz APB can drift several hundred ppm and cause ES8311 jitter.

static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;

static bool i2s_audio_init() {
    i2s_config_t cfg          = {};
    cfg.mode                  = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate           = 48000;
    cfg.bits_per_sample       = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format        = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format  = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags      = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_desc_num          = 8;
    cfg.dma_frame_num         = 256;
    cfg.use_apll              = true;
    cfg.fixed_mclk            = 12288000;   // 256 × 48000 Hz
    cfg.tx_desc_auto_clear    = true;
    cfg.mclk_multiple         = I2S_MCLK_MULTIPLE_256;

    esp_err_t r1 = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    if (r1 != ESP_OK) {
        Logger::info("Audio", String("i2s_driver_install err=") + esp_err_to_name(r1));
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_MCLK;
    pins.bck_io_num   = I2S_BCLK;
    pins.ws_io_num    = I2S_LRCK;
    pins.data_out_num = I2S_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    esp_err_t r2 = i2s_set_pin(I2S_PORT, &pins);
    if (r2 != ESP_OK) {
        Logger::info("Audio", String("i2s_set_pin err=") + esp_err_to_name(r2));
        return false;
    }

    i2s_zero_dma_buffer(I2S_PORT);
    Logger::info("Audio", String("I2S OK port=") + (int)I2S_PORT
        + " 48kHz/16bit/stereo  MCLK=GPIO" + I2S_MCLK
        + " BCLK=GPIO" + I2S_BCLK
        + " LRCK=GPIO" + I2S_LRCK
        + " DOUT=GPIO" + I2S_DOUT
        + "  APLL+fixed_mclk=12288000");
    return true;
}

// ── Tone synthesis task ───────────────────────────────────────────────────────

struct ToneCmd { uint16_t freq; uint16_t ms; };

static constexpr uint32_t SR    = 48000;
static constexpr size_t   CHUNK = 256;
static constexpr int      RAMP  = 240;  // ~5 ms soft attack/decay

static void flush_silence() {
    static const int16_t silence[CHUNK * 2] = {};
    size_t w = 0;
    i2s_write(I2S_PORT, silence, sizeof(silence), &w, pdMS_TO_TICKS(100));
}

void Audio::toneTask(void *param) {
    auto    *self = static_cast<Audio *>(param);
    int16_t  buf[CHUNK * 2];
    ToneCmd  cmd;

    Logger::info("Audio", "Tone task gestartet (Core 1)");

    for (;;) {
        if (xQueueReceive(self->_queue, &cmd, portMAX_DELAY) != pdTRUE) continue;

        if (cmd.freq == 0) {
            // Timed silence (used for gaps in playWarningTone etc.)
            uint32_t frames = (uint32_t)SR * cmd.ms / 1000;
            static const int16_t zeros[CHUNK * 2] = {};
            while (frames > 0) {
                size_t n = (frames < CHUNK) ? (size_t)frames : CHUNK;
                size_t w = 0;
                i2s_write(I2S_PORT, zeros, n * 4, &w, pdMS_TO_TICKS(200));
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
                float   env = (ramp_n < RAMP) ? (float)ramp_n / (float)RAMP : 1.0f;
                if (ramp_n < RAMP) ramp_n++;
                int16_t s      = (int16_t)(sinf(phase) * amp * env);
                buf[i * 2]     = s;
                buf[i * 2 + 1] = s;
                phase += step;
                if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
            }
            for (size_t i = n; i < CHUNK; i++) buf[i * 2] = buf[i * 2 + 1] = 0;

            size_t written = 0;
            i2s_write(I2S_PORT, buf, CHUNK * 4, &written, pdMS_TO_TICKS(500));
            total -= (uint32_t)n;
        }
        flush_silence();
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

Audio audio_obj;
Audio::Audio() : volume_level(70), is_initialized(false), _queue(nullptr) {}

void Audio::init(TwoWire &wire) {
    s_wire = &wire;
    _queue = xQueueCreate(16, sizeof(ToneCmd));

    wire.beginTransmission(ES8311_ADDR);
    uint8_t i2c_err = wire.endTransmission();
    Logger::info("Audio", String("ES8311 I2C 0x18 → ")
        + (i2c_err == 0 ? "OK" : String("FEHLER code=") + i2c_err));
    if (i2c_err != 0) {
        Logger::info("Audio", "ES8311 nicht erreichbar – Audio deaktiviert");
        return;
    }

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

// ── Playback ──────────────────────────────────────────────────────────────────

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

void Audio::playSuccessTone()  { playTone(880, 80);  playTone(1320, 130); }
void Audio::playErrorTone()    { playTone(320, 120); playTone(200,  280); }
void Audio::playWarningTone()  { playTone(880, 80);  playTone(0, 60); playTone(880, 80); }
void Audio::playStartupTone()  { playTone(523, 80);  playTone(659, 80); playTone(784, 130); }
void Audio::stopTone()         { if (_queue) xQueueReset(_queue); }

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
