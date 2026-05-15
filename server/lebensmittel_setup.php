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

// ---- Connect as root ----
try {
    $pdo = new PDO(
        "mysql:host=localhost;charset=utf8mb4",
        $rootUser, $rootPass,
        [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]
    );
} catch (PDOException $e) {
    echo json_encode(['ok' => false, 'error' => 'Root-Login fehlgeschlagen: ' . $e->getMessage()]);
    exit;
}

// ---- Check if DB already exists ----
$stmt = $pdo->prepare("SELECT SCHEMA_NAME FROM information_schema.SCHEMATA WHERE SCHEMA_NAME = ?");
$stmt->execute([$dbName]);
if ($stmt->fetch()) {
    echo json_encode([
        'ok'    => false,
        'error' => "Datenbank „{$dbName}" ist bereits vorhanden. Einrichtung abgebrochen – vorhandene Daten werden nicht verändert."
    ]);
    exit;
}

// ---- Create database ----
$pdo->exec("CREATE DATABASE `{$dbName}` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci");

// ---- Create tables ----
$pdo->exec("USE `{$dbName}`");
$pdo->exec("
CREATE TABLE inventory_events (
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
    device_ts    BIGINT        DEFAULT 0,
    server_ts    DATETIME      DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_barcode   (barcode),
    INDEX idx_label     (label_barcode),
    INDEX idx_household (household),
    INDEX idx_device    (device_name),
    INDEX idx_server_ts (server_ts)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci
");

// Current inventory view
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
      WHERE r.event_type = 'REMOVE_LABEL'
        AND r.label_barcode = e.label_barcode
        AND r.server_ts >= e.server_ts
  )
");

// ---- Create sync user ----
// Drop user if it exists (idempotent for re-runs after partial failure)
try { $pdo->exec("DROP USER IF EXISTS `{$syncUser}`@'%'"); } catch (Exception $e) {}

$escapedPass = str_replace("'", "\\'", $syncPass);
$pdo->exec("CREATE USER `{$syncUser}`@'%' IDENTIFIED BY '{$escapedPass}'");
$pdo->exec("GRANT SELECT, INSERT, UPDATE ON `{$dbName}`.* TO `{$syncUser}`@'%'");
$pdo->exec("FLUSH PRIVILEGES");

echo json_encode([
    'ok'       => true,
    'message'  => "Datenbank „{$dbName}" und Benutzer „{$syncUser}" erfolgreich angelegt.",
    'syncUser' => $syncUser,
    'dbName'   => $dbName,
]);
