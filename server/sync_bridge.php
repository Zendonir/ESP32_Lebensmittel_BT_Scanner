<?php
/**
 * Lebensmittel_Scanner – Sync Bridge
 *
 * Deploy this file on your web server alongside lebensmittel_setup.php.
 * The ESP32 sends credentials in X-DB-User / X-DB-Pass headers – no
 * credentials need to be hardcoded here.
 *
 * Requirements: PHP 7.4+, PDO with MySQL driver
 */

define('DB_HOST', 'localhost');
define('DB_NAME', 'Lebensmittel_Scanner');

header('Content-Type: application/json');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Headers: Content-Type, X-DB-User, X-DB-Pass');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') { http_response_code(204); exit; }

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(['ok' => false, 'error' => 'Method not allowed']);
    exit;
}

// ---- credentials from request headers ----
$dbUser = trim($_SERVER['HTTP_X_DB_USER'] ?? '');
$dbPass = $_SERVER['HTTP_X_DB_PASS'] ?? '';

if (empty($dbUser)) {
    http_response_code(401);
    echo json_encode(['ok' => false, 'error' => 'X-DB-User header fehlt']);
    exit;
}

// ---- parse body ----
$body = file_get_contents('php://input');
$data = json_decode($body, true);
if (!$data) {
    http_response_code(400);
    echo json_encode(['ok' => false, 'error' => 'Invalid JSON']);
    exit;
}

$type = $data['type'] ?? 'UNKNOWN';

// PING – test credentials without writing to DB
if ($type === 'PING') {
    try {
        $pdo = new PDO(
            "mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4",
            $dbUser, $dbPass,
            [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION]
        );
        echo json_encode(['ok' => true, 'message' => 'pong']);
    } catch (PDOException $e) {
        echo json_encode(['ok' => false, 'error' => 'DB-Login fehlgeschlagen: ' . $e->getMessage()]);
    }
    exit;
}

// ---- connect to DB ----
try {
    $pdo = new PDO(
        "mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4",
        $dbUser, $dbPass,
        [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC]
    );
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['ok' => false, 'error' => 'DB-Verbindung fehlgeschlagen: ' . $e->getMessage()]);
    exit;
}

// ---- insert event ----
try {
    $stmt = $pdo->prepare("
        INSERT INTO inventory_events
            (event_type, device_name, household, barcode, label_barcode,
             name, brand, category, expiry_date, added_date, quantity, device_ts)
        VALUES
            (:event_type, :device_name, :household, :barcode, :label_barcode,
             :name, :brand, :category, :expiry_date, :added_date, :quantity, :device_ts)
    ");

    $stmt->execute([
        ':event_type'    => strtoupper($type),
        ':device_name'   => substr($data['deviceName']   ?? '', 0, 100),
        ':household'     => substr($data['household']    ?? 'Standard', 0, 100),
        ':barcode'       => substr($data['barcode']      ?? '', 0, 100),
        ':label_barcode' => substr($data['labelBarcode'] ?? '', 0, 100),
        ':name'          => substr($data['name']         ?? '', 0, 255),
        ':brand'         => substr($data['brand']        ?? '', 0, 255),
        ':category'      => substr($data['category']     ?? '', 0, 100),
        ':expiry_date'   => substr($data['expiryDate']   ?? '', 0, 20),
        ':added_date'    => substr($data['addedDate']    ?? '', 0, 20),
        ':quantity'      => intval($data['quantity']     ?? 1),
        ':device_ts'     => intval($data['timestamp']    ?? 0),
    ]);

    echo json_encode(['ok' => true, 'id' => $pdo->lastInsertId()]);

} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['ok' => false, 'error' => $e->getMessage()]);
}
