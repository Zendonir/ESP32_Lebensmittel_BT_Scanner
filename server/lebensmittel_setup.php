<?php
/**
 * Lebensmittel_Scanner – Einrichtungsassistent (Setup Wizard)
 *
 * Deploy this file on your web server alongside sync_bridge.php.
 * This script is called ONCE by the ESP32 setup wizard.
 * It creates the database, tables and a dedicated sync user.
 *
 * Root credentials are sent only once and never stored.
 */

header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Headers: Content-Type, X-Device-Id');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') { http_response_code(204); exit; }
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(['ok' => false, 'error' => 'Method not allowed']);
    exit;
}

$body = file_get_contents('php://input');
$data = json_decode($body, true);
if (!$data) {
    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'Ungültige JSON-Daten']);
    exit;
}

$rootUser = trim($data['rootUser'] ?? '');
$rootPass = $data['rootPass'] ?? '';
$syncPass = trim($data['syncPass'] ?? '');
$syncUser = 'lebensmittel_sync';
$dbName   = 'Lebensmittel_Scanner';

if (empty($rootUser) || empty($syncPass)) {
    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'rootUser und syncPass sind erforderlich']);
    exit;
}

// ---- Step 1: Connect as root (no database selected) ----
try {
    $pdoRoot = new PDO(
        "mysql:host=localhost;charset=utf8mb4",
        $rootUser, $rootPass,
        [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]
    );
} catch (PDOException $e) {
    echo json_encode(['ok' => false, 'error' => 'Root-Login fehlgeschlagen: ' . $e->getMessage()]);
    exit;
}

// ---- Step 2: Check if DB already exists ----
$stmt = $pdoRoot->prepare("SELECT SCHEMA_NAME FROM information_schema.SCHEMATA WHERE SCHEMA_NAME = ?");
$stmt->execute([$dbName]);
if ($stmt->fetch()) {
    echo json_encode([
        'ok'    => false,
        'error' => "Datenbank „{$dbName}" ist bereits vorhanden. Einrichtung abgebrochen – vorhandene Daten werden nicht verändert."
    ]);
    exit;
}

// ---- Step 3: Create database ----
try {
    $pdoRoot->exec("CREATE DATABASE `{$dbName}` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");
} catch (PDOException $e) {
    echo json_encode(['ok' => false, 'error' => 'Datenbank konnte nicht erstellt werden: ' . $e->getMessage()]);
    exit;
}

// ---- Step 4: Reconnect with the new database in the DSN ----
// DO NOT use "USE database" — reconnecting with dbname in DSN is reliable across all MySQL versions.
try {
    $pdo = new PDO(
        "mysql:host=localhost;dbname={$dbName};charset=utf8mb4",
        $rootUser, $rootPass,
        [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]
    );
} catch (PDOException $e) {
    echo json_encode(['ok' => false, 'error' => 'Verbindung zur neuen Datenbank fehlgeschlagen: ' . $e->getMessage()]);
    exit;
}

// ---- Step 5: Create table ----
try {
    $pdo->exec("
        CREATE TABLE inventory_events (
            id            INT AUTO_INCREMENT PRIMARY KEY,
            event_type    ENUM('ADD','REMOVE_LABEL','REMOVE_BARCODE','PING') NOT NULL,
            device_name   VARCHAR(100)  NOT NULL DEFAULT '',
            household     VARCHAR(100)  NOT NULL DEFAULT 'Standard',
            barcode       VARCHAR(100)  NOT NULL DEFAULT '',
            label_barcode VARCHAR(100)  NOT NULL DEFAULT '',
            name          VARCHAR(255)  NOT NULL DEFAULT '',
            brand         VARCHAR(255)  NOT NULL DEFAULT '',
            category      VARCHAR(100)  NOT NULL DEFAULT '',
            expiry_date   VARCHAR(20)   NOT NULL DEFAULT '',
            added_date    VARCHAR(20)   NOT NULL DEFAULT '',
            quantity      INT           NOT NULL DEFAULT 1,
            device_ts     BIGINT        NOT NULL DEFAULT 0,
            server_ts     DATETIME      NOT NULL DEFAULT CURRENT_TIMESTAMP,
            INDEX idx_barcode   (barcode),
            INDEX idx_label     (label_barcode),
            INDEX idx_household (household),
            INDEX idx_device    (device_name),
            INDEX idx_server_ts (server_ts)
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
    ");
} catch (PDOException $e) {
    echo json_encode(['ok' => false, 'error' => 'Tabelle konnte nicht erstellt werden: ' . $e->getMessage()]);
    exit;
}

// ---- Step 6: Create view ----
try {
    $pdo->exec("
        CREATE VIEW current_inventory AS
        SELECT
            e.label_barcode, e.barcode, e.name, e.brand, e.category,
            e.expiry_date, e.added_date, e.quantity, e.household, e.device_name,
            e.server_ts AS stored_at
        FROM inventory_events e
        WHERE e.event_type = 'ADD'
          AND e.label_barcode NOT IN (
              SELECT r.label_barcode FROM inventory_events r
              WHERE r.event_type  = 'REMOVE_LABEL'
                AND r.label_barcode = e.label_barcode
                AND r.server_ts    >= e.server_ts
          )
    ");
} catch (PDOException $e) {
    // View creation failing is non-fatal – table is the important part
    error_log("Lebensmittel_Scanner: view creation failed: " . $e->getMessage());
}

// ---- Step 7: Create sync user ----
try {
    // Use root connection for user management (not restricted to one DB)
    try { $pdoRoot->exec("DROP USER IF EXISTS `{$syncUser}`@'%'"); } catch (Exception $ignore) {}

    // Use a prepared-statement-safe approach for the password
    $pdoRoot->exec("CREATE USER `{$syncUser}`@'%' IDENTIFIED BY " . $pdoRoot->quote($syncPass));
    $pdoRoot->exec("GRANT SELECT, INSERT, UPDATE ON `{$dbName}`.* TO `{$syncUser}`@'%'");
    $pdoRoot->exec("FLUSH PRIVILEGES");
} catch (PDOException $e) {
    echo json_encode(['ok' => false, 'error' => 'Benutzer konnte nicht angelegt werden: ' . $e->getMessage()]);
    exit;
}

echo json_encode([
    'ok'       => true,
    'message'  => "Datenbank „{$dbName}" und Benutzer „{$syncUser}" erfolgreich angelegt.",
    'syncUser' => $syncUser,
    'dbName'   => $dbName,
]);
