#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "display.h"
#include "AppStateManager.h"
#include "../models/ProductTemplate.h"
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
#include "../network/NtfyNotifier.h"
#include "../models/InventoryItem.h"
#include "../storage/BackupManager.h"

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
        TMPL_SORTE,          // sorte/variety picker (with "Neu erstellen" at top)
        TMPL_AMOUNT,         // numpad entry for food amount (Stück/Gramm) if template has unit
        TMPL_MHD,            // MHD confirm before saving
        // Fully manual keyboard entry
        KB_ENTRY,            // keyboard input for product name
        // Location selection
        LOCATION_SELECT,     // showing location list to activate
        // WiFi setup via touchscreen
        WIFI_SETUP_SCAN,     // showing "scanning…" screen, waiting for async scan
        WIFI_SETUP_LIST,     // showing SSID list
        WIFI_SETUP_PASS,     // showing keyboard for password entry
        WIFI_SETUP_CONFIRM,  // "Weiter?" confirmation before connecting
        WIFI_SETUP_CONN,     // connecting, polling for result
        // Inventory text search
        INV_SEARCH,          // on-screen keyboard for filtering inventory list
        // SD backup import prompt (shown at startup)
        SD_IMPORT_PROMPT,
        // FIFO out-of-order warning (waiting for user confirmation)
        FIFO_WARN,
        // Label roll management
        NEW_ROLL_ENTRY,    // numpad to enter roll size
        NEW_ROLL_CONFIRM,  // "same roll type as before?" dialog
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
    uint32_t buildUiHash() const;
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
    void showTmplSorten();
    void proceedFromSorte();
    void addSorteToTemplate(const String &templateId, const String &sorte);
    void startTmplMHD();
    String calcMHD(int shelfDays, int offset) const;

    // Location helpers
    std::vector<String> loadLocationNames() const;
    String              loadLocationColor(const String &name) const;
    void showLocationSelect();
    void doInventoryPull();

    // Inventory search helper
    String bestMatchForSearch(const String &query) const;

    // Manual name autocomplete
    void   loadManualNames();
    void   saveManualName(const String &name);
    String bestMatchForName(const String &query) const;
    String bestMatchForSorte(const String &query) const;
    String kbEntryTitle() const;
    String kbEntrySuggestion() const;

    TwoWire         i2c_bus;
    AppStateManager state;
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
    uint32_t        _lastUiHash = 0;
    UiTab           _activeTab = UiTab::STORE;
    uint32_t        _standbyMs = 0;        // 0 = never; 120000 / 300000 ms
    uint32_t        _lastActivityMs = 0;
    std::atomic<bool> _displayOn{true};
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
    TaskHandle_t    _fetchTaskHandle = nullptr;
    uint32_t        _fetchStartedMs  = 0;
    static constexpr uint32_t FETCH_TIMEOUT_MS = 30000;

    // Template workflow state
    std::vector<ProductTemplate> _templates;
    String   _selectedCategory;
    int      _selectedTemplateIdx = -1;
    String   _selectedBrand;             // brand chosen in TMPL_BRAND step
    String   _selectedSorte;            // sorte/variety chosen in TMPL_SORTE step
    bool     _creatingNewSorte = false; // true while KB_ENTRY is used to type a new sorte
    int      _mhdOffset           = 0;   // ±days applied to template's shelfDays
    String   _pendingUnit;               // unit from template (e.g. "g", "St.") – empty = no amount step
    String   _pendingAmountDraft;        // digits typed on TMPL_AMOUNT numpad
    int      _pendingAmount       = 1;   // confirmed food amount (pairs with _pendingUnit)

    // Manual name autocomplete
    std::vector<String> _manualNames;

    // Keyboard entry state
    String   _kbText;                    // current keyboard input text
    String   _invFilter;                 // inventory list text filter
    String   _invExpandedGroup;          // name of currently expanded inventory group (empty = none)

    // WiFi setup state
    std::vector<String> _wifiNets;
    String              _selectedSsid;
    uint32_t            _wifiConnectStartMs = 0;

    // Inventory pull sync timer
    uint32_t _lastInventorySyncMs = 0;
    static constexpr uint32_t INVENTORY_SYNC_INTERVAL_MS = 120000; // 2 minutes

    // Ntfy push notification timer
    uint32_t _lastNtfyCheckMs = 0;
    static constexpr uint32_t NTFY_CHECK_INTERVAL_MS = 21600000UL; // 6 hours

    // SD daily backup timer
    uint32_t _lastBackupCheckMs = 0;
    static constexpr uint32_t BACKUP_CHECK_INTERVAL_MS = 3600000UL; // check every hour

    // Result screen auto-dismiss
    uint32_t _resultShownMs = 0;
    static constexpr uint32_t RESULT_AUTO_DISMISS_MS = 5000;

    int countExpiringSoon(int days = 7) const;

    // Label roll entry state
    String   _rollDraft;
    uint32_t _prevRollSize = 0;

    // Scanner battery polling
    uint32_t _lastBatteryPollMs  = 0;
    bool     _batteryWarnShown   = false;
    static constexpr uint32_t BATTERY_POLL_MS = 300000UL; // every 5 min

    // FIFO warning pending state
    String _fifoLabelCode;
    String _fifoProductName;
    String _fifoEanBarcode;
    String _fifoOlderExpiry;
    String _fifoRemovedExpiry;
};

extern App app;
