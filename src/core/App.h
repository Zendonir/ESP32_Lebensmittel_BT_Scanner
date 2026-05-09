#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <DNSServer.h>
#include "display.h"
#include "AppStateManager.h"
#include "EventBus.h"
#include "TimeManager.h"
#include "../storage/LittleFSManager.h"
#include "../storage/JsonStorage.h"
#include "../web/WebInterface.h"
#include "../scanner/BarcodeManager.h"
#include "../network/ApiClient.h"
#include "../network/OpenFoodFacts.h"
#include "../inventory/InventoryStorage.h"
#include "../inventory/InventoryManager.h"
#include "../inventory/LabelCounter.h"
#include "../printer/PrinterManager.h"

class App {
public:
    App();
    void begin();
    void loop();

private:
    enum class WorkflowMode {
        HOME,
        FETCHING_PRODUCT,
        ENTER_DATE,
        ENTER_QTY,
        SAVING,
        RESULT
    } workflow = WorkflowMode::HOME;

    void initBacklight();
    void initI2C();
    void initFilesystem();
    void initWebServer();
    void handleSerialCommand(const String &command);
    void renderWiFiStatus();
    void renderDashboard(const String &message = "");
    void renderActiveTab(const String &message = "");
    void handleTouch();
    void processOnscreenAction(OnscreenAction action);
    void handleScan(const ScanResult &scan);
    void startProductLookup(const String &barcode);
    void processWorkflow();
    bool finishStorageWorkflow();
    bool formatDateDraft(String &formatted) const;
    char digitForAction(OnscreenAction action) const;

    TwoWire         i2c_bus;
    AppStateManager state;
    EventBus        events;
    TimeManager     time_manager;
    LittleFSManager fs;
    JsonStorage     json;
    InventoryStorage inventoryStorage;
    InventoryManager inventory;
    ApiClient       api;
    OpenFoodFacts   openFoodFacts;
    LabelCounter    labelCounter;
    PrinterManager  printer;
    WebInterface    web;
    DNSServer       _dns;
    bool            _dnsRunning = false;
    bool            _touchWasPressed = false;
    uint32_t        _lastTouchMs = 0;
    uint32_t        _lastUiRefreshMs = 0;
    String          _statusMessage;
    UiTab           _activeTab = UiTab::STORE;
    ProductInfo     _pendingProduct;
    String          _pendingDateDraft;
    String          _pendingExpiryDate;
    int             _pendingQuantity = 1;
    String          _pendingBarcode;
    String          _resultTitle;
    String          _resultMessage;
    bool            _resultSuccess = false;
};

extern App app;
