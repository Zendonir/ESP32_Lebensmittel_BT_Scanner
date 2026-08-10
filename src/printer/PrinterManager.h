#pragma once

#include <Arduino.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "EscPosPrinter.h"
#include "LabelRenderer.h"

class PrinterManager {
public:
    PrinterManager();
    void begin();
    void configure(uint32_t baud);
    bool isReady() const;
    uint32_t baud() const;
    void feed(uint8_t lines);
    void setPostFeed(uint16_t lines);
    bool printLabel(const InventoryItem &item);

    // Druckauftrag einreihen statt sofort drucken. Ein Etikett braucht je nach
    // Baudrate mehrere hundert Millisekunden; im Web-Task würde das den ganzen
    // HTTP-Server blockieren. Die Warteschlange wird von App::loop() (Core 1)
    // abgearbeitet. Gibt false zurück, wenn die Queue voll ist.
    bool   queueLabel(const InventoryItem &item);
    size_t queuedLabels() const;
    void   processQueue();   // druckt höchstens ein Etikett pro Aufruf
    bool printLast();
    bool hasLastPrint() const { return _hasLastPrint; }
    void printManual(const String &text, bool center = false);
    void printManualLabel(const String &text, bool center = false);
    size_t printTestPage(bool includeQr = false);
    size_t printPlainTest();
    size_t printBaudProbe();

private:
    EscPosPrinter printer;
    LabelRenderer renderer;
    uint32_t currentBaud  = 0;
    uint16_t postFeedDots = 86;
    InventoryItem _lastItem;
    bool _hasLastPrint = false;

    // Druckwarteschlange (Web-Task schreibt, App::loop() liest)
    static constexpr size_t MAX_QUEUE = 20;
    std::vector<InventoryItem> _queue;
    mutable SemaphoreHandle_t  _queueMutex = nullptr;
};
