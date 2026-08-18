<?php
header('Content-Type: application/json; charset=utf-8');

$endpoint = $_GET['endpoint'] ?? 'healthz';

if ($endpoint === 'healthz') {
    echo json_encode([
        'status' => 'HEALTHY',
        'timestamp' => time(),
        'server' => 'GMO itpark (gontaro.org)',
        'php_version' => PHP_VERSION,
        'sqlite' => extension_loaded('pdo_sqlite')
    ]);
    exit;
}

// データベース接続テスト
try {
    $pdo = new PDO("sqlite:" . __DIR__ . "/iot_platform.db");
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $pdo->exec("CREATE TABLE IF NOT EXISTS test_tbl (id INTEGER PRIMARY KEY, msg TEXT)");
    $pdo->exec("INSERT INTO test_tbl (msg) VALUES ('test_ok')");
    $cnt = $pdo->query("SELECT count(*) FROM test_tbl")->fetchColumn();

    echo json_encode([
        'status' => 'DB_OK',
        'records' => (int)$cnt
    ]);
} catch (Throwable $e) {
    echo json_encode([
        'status' => 'DB_ERROR',
        'error' => $e->getMessage()
    ]);
}
