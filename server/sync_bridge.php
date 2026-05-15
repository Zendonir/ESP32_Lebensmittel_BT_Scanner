<?php
/**
 * Lebensmittel_Scanner – Sync Bridge
 *
 * Deploy this file on your web server.
 * Set the ESP32 Server-Sync URL to:  https://your-server.example.com/sync_bridge.php
 *
 * Requirements: PHP 7.4+, PDO with MySQL driver
 */

define('DB_HOST', 'localhost');
define('DB_NAME', 'Lebensmittel_Scanner');
define('DB_USER', 'lebensmittel');   // change to your DB user
define('DB_PASS', 'changeme');       // change to your DB password

// Optional: shared secret header (set same value in future firmware extension)
define('API_SECRET', '');  // leave empty to disable check

header('Content-Type: application/json');

// ---- secret check ----
if (API_SECRET !== '') {
    $hdr = $_SERVER['HTTP_X_API_SECRET'] ?? '';
    if ($hdr !== API_SECRET) {
        http_response_code(403);
        echo json_encode(['ok' => false, 'error' => 'Forbidden']);
        exit;
    }
}

// ---- only POST ----
if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    echo json_encode(['ok' => false, 'error' => 'Method not allowed']);
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

// PING – just acknowledge
if ($type === 'PING') {
    echo json_encode(['ok' => true, 'message' => 'pong']);
    exit;
}

// ---- connect to DB ----
try {
    $dsn = "mysql:host=" . DB_HOST . ";dbname=" . DB_NAME . ";charset=utf8mb4";
    $pdo = new PDO($dsn, DB_USER, DB_PASS, [
        PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
        PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
    ]);
} catch (PDOException $e) {
    http_response_code(500);
    echo json_encode(['ok' => false, 'error' => 'DB connection failed: ' . $e->getMessage()]);
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
