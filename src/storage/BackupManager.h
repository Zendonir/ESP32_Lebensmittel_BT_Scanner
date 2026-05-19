#pragma once
#include <Arduino.h>

namespace BackupManager {
    static constexpr const char* BACKUP_DIR  = "/scanner_backup";
    static constexpr const char* INFO_FILE   = "/scanner_backup/backup_info.json";

    // Files copied to/from SD backup
    static const char* const BACKUP_FILES[] = {
        "/inventory.json",
        "/custom_products.json",
        "/locations.json",
        "/server_sync_config.json",
        "/display_config.json",
        "/printer_config.json",
        nullptr
    };

    bool hasBackupOnSD();        // SD card has backup files
    bool internalIsEmpty();      // internal inventory.json absent or empty array
    bool doBackup();             // internal UserData → SD
    bool doRestore();            // SD → internal UserData
    String backupDateString();   // human-readable timestamp of last backup
    void runDailyCheck();        // call from loop() — backs up if ≥24 h since last
}
