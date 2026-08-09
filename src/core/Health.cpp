#include "Health.h"
#include "Logger.h"

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_app_format.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

// 60 s ist bewusst großzügig: einzelne Loop-Durchläufe dürfen legitim mehrere
// Sekunden dauern (SD-Backup, LittleFS-Schreibvorgang). Erst wenn gar nichts
// mehr passiert, greift der Watchdog.
constexpr uint32_t WDT_TIMEOUT_S = 60;

constexpr uint32_t STATS_INTERVAL_MS = 60000;

// Schwellwerte für den internen (DRAM) Heap. WiFi + BLE + AsyncWebServer
// brauchen zusammen bereits einen zweistelligen KB-Bereich als Luft.
constexpr uint32_t HEAP_WARN_BYTES  = 45000;
constexpr uint32_t BLOCK_WARN_BYTES = 20000;

bool     s_wdtReady   = false;
uint32_t s_lastStats  = 0;
uint32_t s_minFreeSeen = UINT32_MAX;
bool     s_heapWarned = false;

const char *resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:  return "Power-On";
        case ESP_RST_EXT:      return "Externer Reset";
        case ESP_RST_SW:       return "Software-Neustart (ESP.restart)";
        case ESP_RST_PANIC:    return "PANIC / Exception";
        case ESP_RST_INT_WDT:  return "Interrupt-Watchdog";
        case ESP_RST_TASK_WDT: return "Task-Watchdog";
        case ESP_RST_WDT:      return "Sonstiger Watchdog";
        case ESP_RST_DEEPSLEEP:return "Deep-Sleep-Aufwachen";
        case ESP_RST_BROWNOUT: return "BROWNOUT (Spannungseinbruch)";
        case ESP_RST_SDIO:     return "SDIO-Reset";
        default:               return "Unbekannt";
    }
}

}  // namespace

namespace Health {

void logBootInfo() {
    esp_reset_reason_t r = esp_reset_reason();
    String msg = String("Reset-Ursache: ") + resetReasonName(r) + " (" + (int)r + ")";
    // WARN/ERROR landen auch auf der SD-Karte – genau das braucht die Fehlersuche
    // nach einem unbeobachteten Neustart.
    if (r == ESP_RST_PANIC || r == ESP_RST_TASK_WDT || r == ESP_RST_INT_WDT
            || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT)
        Logger::error("Health", msg);
    else
        Logger::warn("Health", msg);

    Logger::warn("Health", String("Boot Heap: intern ")
                 + (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024) + " KB frei, größter Block "
                 + (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024) + " KB, PSRAM "
                 + (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024) + " KB frei");
}

void beginWatchdog() {
    // Arduino/IDF initialisiert den Task-Watchdog je nach sdkconfig bereits.
    // In dem Fall liefert esp_task_wdt_init ESP_ERR_INVALID_STATE und wir passen
    // die bestehende Konfiguration stattdessen an.
    esp_task_wdt_config_t cfg = {
        .timeout_ms    = WDT_TIMEOUT_S * 1000,
        .idle_core_mask = 0,       // Idle-Tasks NICHT überwachen (sonst Fehlalarme)
        .trigger_panic = true,     // Panic → Neustart mit Backtrace im Coredump
    };
    esp_err_t err = esp_task_wdt_init(&cfg);
    if (err == ESP_ERR_INVALID_STATE) err = esp_task_wdt_reconfigure(&cfg);
    if (err != ESP_OK) {
        Logger::warn("Health", String("Task-Watchdog nicht aktivierbar: ") + esp_err_to_name(err));
        return;
    }
    s_wdtReady = true;
    watchCurrentTask();
    Logger::info("Health", String("Task-Watchdog aktiv (") + WDT_TIMEOUT_S + " s)");
}

bool watchdogActive() { return s_wdtReady; }

void watchCurrentTask() {
    if (!s_wdtReady) return;
    esp_err_t err = esp_task_wdt_add(nullptr);
    if (err != ESP_OK && err != ESP_ERR_INVALID_ARG)  // INVALID_ARG = bereits angemeldet
        Logger::warn("Health", String("esp_task_wdt_add: ") + esp_err_to_name(err));
}

void feed() {
    if (s_wdtReady) esp_task_wdt_reset();
}

uint32_t freeInternalHeap() {
    return (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

bool lowMemory() {
    return freeInternalHeap() < HEAP_WARN_BYTES
        || (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) < BLOCK_WARN_BYTES;
}

void loop() {
    feed();

    uint32_t now = millis();
    if (now - s_lastStats < STATS_INTERVAL_MS) return;
    s_lastStats = now;

    uint32_t freeInt  = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t largest  = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    uint32_t freePs   = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t stackLo  = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
    if (freeInt < s_minFreeSeen) s_minFreeSeen = freeInt;

    bool low = (freeInt < HEAP_WARN_BYTES) || (largest < BLOCK_WARN_BYTES);
    String msg = String("Heap intern ") + freeInt / 1024 + " KB (min " + s_minFreeSeen / 1024
               + " KB, größter Block " + largest / 1024 + " KB), PSRAM " + freePs / 1024
               + " KB, Loop-Stack frei " + stackLo + " B, Uptime " + now / 60000 + " min";

    if (low && !s_heapWarned) {
        s_heapWarned = true;
        Logger::error("Health", "Speicher wird knapp! " + msg);
    } else if (!low && s_heapWarned) {
        s_heapWarned = false;
        Logger::warn("Health", "Speicher wieder ausreichend. " + msg);
    } else {
        Logger::info("Health", msg);
    }

    // Ein zu knapper Loop-Stack ist die klassische Ursache für scheinbar
    // zufällige Abstürze – lieber laut warnen, bevor er überläuft.
    if (stackLo < 1024)
        Logger::error("Health", String("Loop-Stack fast erschöpft: nur ") + stackLo + " B frei");
}

}  // namespace Health
