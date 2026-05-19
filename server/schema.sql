-- Lebensmittel_Scanner MySQL Schema
-- Deploy on your server and point the ESP32 sync URL to sync_bridge.php

CREATE DATABASE IF NOT EXISTS Lebensmittel_Scanner
    CHARACTER SET utf8mb4
    COLLATE utf8mb4_unicode_ci;

USE Lebensmittel_Scanner;

CREATE TABLE IF NOT EXISTS inventory_events (
    id           INT AUTO_INCREMENT PRIMARY KEY,
    event_type   ENUM('ADD','REMOVE_LABEL','REMOVE_BARCODE','PING') NOT NULL,
    device_name  VARCHAR(100)  DEFAULT '',
    household    VARCHAR(100)  DEFAULT 'Standard',
    barcode      VARCHAR(100)  DEFAULT '',
    label_barcode VARCHAR(100) DEFAULT '',
    name         VARCHAR(255)  DEFAULT '',
    brand        VARCHAR(255)  DEFAULT '',
    category     VARCHAR(100)  DEFAULT '',
    expiry_date  VARCHAR(20)   DEFAULT '',
    added_date   VARCHAR(20)   DEFAULT '',
    quantity     INT           DEFAULT 1,
    device_ts    BIGINT        DEFAULT 0,   -- Unix timestamp from ESP32
    server_ts    DATETIME      DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_barcode      (barcode),
    INDEX idx_label        (label_barcode),
    INDEX idx_household    (household),
    INDEX idx_device       (device_name),
    INDEX idx_server_ts    (server_ts)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Current inventory view: latest state per label barcode
CREATE OR REPLACE VIEW current_inventory AS
SELECT
    e.label_barcode,
    e.barcode,
    e.name,
    e.brand,
    e.category,
    e.expiry_date,
    e.added_date,
    e.quantity,
    e.household,
    e.device_name,
    e.server_ts AS stored_at
FROM inventory_events e
WHERE e.event_type = 'ADD'
  AND e.label_barcode NOT IN (
      SELECT r.label_barcode
      FROM inventory_events r
      WHERE r.event_type = 'REMOVE_LABEL'
        AND r.label_barcode = e.label_barcode
        AND r.server_ts >= e.server_ts
  );
