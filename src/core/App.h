#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <vector>
#include "display.h"
#include "AppStateManager.h"
#include "../models/ProductTemplate.h"
#include "EventBus.h"
#include "TimeManager.h"
#include "DeviceConfig.h"
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
#include "../network/SyncManager.h"

class App {
public:
    App();
    void begin();
    void loop();
    void loadDisplayConfig();   // called from WebInterface after display-config POST

private:
    enum class WorkflowMode {
        HOME,
        FETCHING_PRODUCT,
        ENTER_DATE,
        ENTER_QTY,
        SAVING,
        RESULT,
        // Template-based product selection
        TMPL_CATEGORY,       // showing category list
        TMPL_PRODUCT,        // showing product list for selected category
        TMPL_BRAND,          // brand selection when template has >1 brand
        TMPL_MHD,            // MHD + qty confirm before saving
        // Fully manual keyboard entry
        KB_ENTRY,            // keyboard input for product name
        // Location selection
        LOCATION_SELECT,     // showing location list to activate
        // WiFi setup via touchscreen
        WIFI_SETUP_SCAN,     // showing "scanning…" screen, waiting for async scan
        WIFI_SETUP_LIST,     // showing SSID list
        WIFI_SETUP_PASS,     // showing keyboard for password entry
        WIFI_SETUP_CONN,     // connecting, polling for result
        // Inventory text search
        INV_SEARCH,          // on-screen keyboard for filtering inventory list
    } workflow = WorkflowMode::HOME;

    void initBacklight();
    void setBacklight(bool on);
    void initI2C();
    void resetLCDViaTCA9554();
    void initFilesystem();
    void initWebServer();
    void handleSerialCommand(const String &command);
    void renderDashboard(const String &message = "");
    void renderActiveTab(const String &message = "", bool force = false);
    String buildUiSignature() const;
    void handleTouch();
    void processOnscreenAction(OnscreenAction action);
    void handleScan(const ScanResult &scan);
    void startProductLookup(const String &barcode);
    void processWorkflow();
    bool finishStorageWorkflow();
    bool formatDateDraft(String &formatted) const;
    char digitForAction(OnscreenAction action) const;
    static void fetchTaskFn(void *param);

    // Template workflow helpers
    void loadTemplates();
    std::vector<String> templateCategories() const;
    std::vector<ProductTemplate> templatesForCategory(const String &cat) const;
    void showTmplCategories();
    void showTmplProducts();
    void showTmplBrands();
    void startTmplMHD();
    String calcMHD(int shelfDays, int offset) const;

    // Location helpers
    std::vector<String> loadLocationNames() const;
    String              loadLocationColor(const String &name) const;
    void showLocationSelect();
    void doInventoryPull();

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
    uint32_t        _lastUiRefreshMs = 0;
    String          _statusMessage;
    String          _lastUiSignature;
    UiTab           _activeTab = UiTab::STORE;
    uint32_t        _standbyMs = 0;        // 0 = never; 120000 / 300000 ms
    uint32_t        _lastActivityMs = 0;
    bool            _displayOn = true;
    ProductInfo     _pendingProduct;
    String          _pendingDateDraft;
    String          _pendingExpiryDate;
    int             _pendingQuantity = 1;
    String          _pendingBarcode;
    String          _resultTitle;
    String          _resultMessage;
    bool            _resultSuccess = false;

    // Async product fetch
    volatile bool   _fetchStarted = false;
    volatile bool   _fetchDone    = false;
    bool            _fetchOk      = false;
    ProductInfo     _fetchedProduct;

    // Template workflow state
    std::vector<ProductTemplate> _templates;
    String   _selectedCategory;
    int      _selectedTemplateIdx = -1;
    String   _selectedBrand;             // brand chosen in TMPL_BRAND step
    int      _mhdOffset           = 0;   // ±days applied to template's shelfDays

    // Keyboard entry state
    String   _kbText;                    // current keyboard input text
    String   _invFilter;                 // inventory list text filter

    // WiFi setup state
    std::vector<String> _wifiNets;
    String              _selectedSsid;
    uint32_t            _wifiConnectStartMs = 0;

    // Inventory pull sync timer
    uint32_t _lastInventorySyncMs = 0;
    static constexpr uint32_t INVENTORY_SYNC_INTERVAL_MS = 120000; // 2 minutes

    // Result screen auto-dismiss
    uint32_t _resultShownMs = 0;
    static constexpr uint32_t RESULT_AUTO_DISMISS_MS = 5000;

    int countExpiringSoon(int days = 7) const;
};

extern App app;
