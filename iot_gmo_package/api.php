<?php
/**
 * ==============================================================================
 * IoT Platform Management API (PHP 8.4 + SQLite Edition)
 * Host: gontaro.org / Path: /iot/
 * ==============================================================================
 */

// エラー詳細を JSON で確認できるように設定
error_reporting(E_ALL);
ini_set('display_errors', '0');

header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type, Authorization');

if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(200);
    exit;
}

try {
    // ルーティング解析
    $endpoint = $_GET['endpoint'] ?? '';
    if (empty($endpoint) && isset($_SERVER['REQUEST_URI'])) {
        $path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);
        if (preg_match('#/iot/api/v1/(.*)#', $path, $matches)) {
            $endpoint = $matches[1];
        } elseif (preg_match('#/iot/(healthz)#', $path, $matches)) {
            $endpoint = $matches[1];
        }
    }

    // 1. ヘルスチェック: /healthz
    if ($endpoint === 'healthz') {
        header('Content-Type: application/json; charset=utf-8');
        echo json_encode([
            'status' => 'HEALTHY',
            'timestamp' => time(),
            'server' => 'GMO itpark (gontaro.org)',
            'php_version' => PHP_VERSION,
            'sqlite_supported' => extension_loaded('pdo_sqlite')
        ]);
        exit;
    }

    // データベース接続 (SQLite PDO)
    $dbFile = __DIR__ . '/iot_platform.db';
    $pdo = new PDO("sqlite:" . $dbFile);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
    $pdo->setAttribute(PDO::ATTR_DEFAULT_FETCH_MODE, PDO::FETCH_ASSOC);

    // テーブル初期化
    $pdo->exec("
        CREATE TABLE IF NOT EXISTS devices (
            device_id TEXT PRIMARY KEY,
            device_type TEXT,
            firmware_version TEXT,
            status TEXT,
            last_seen INTEGER,
            registered_at INTEGER,
            total_telemetries INTEGER DEFAULT 0,
            current_interval_sec INTEGER DEFAULT 15
        );

        CREATE TABLE IF NOT EXISTS telemetry (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT,
            seq_no INTEGER,
            timestamp INTEGER,
            temperature REAL,
            humidity REAL,
            battery_voltage REAL,
            battery_level_pct INTEGER,
            rssi INTEGER,
            interval_sec INTEGER,
            raw_json TEXT
        );

        CREATE TABLE IF NOT EXISTS commands (
            command_id TEXT PRIMARY KEY,
            device_id TEXT,
            action TEXT,
            params_json TEXT,
            status TEXT DEFAULT 'PENDING',
            created_at INTEGER,
            acked_at INTEGER,
            result TEXT
        );
    ");

    $method = $_SERVER['REQUEST_METHOD'] ?? 'GET';

    // 2. テレメトリ受信: POST /api/v1/telemetry
    if ($endpoint === 'telemetry' && $method === 'POST') {
        $rawInput = file_get_contents('php://input');
        $payload = json_decode($rawInput, true);

        if (!$payload || !isset($payload['header']['device_id'])) {
            http_response_code(400);
            header('Content-Type: application/json; charset=utf-8');
            echo json_encode(['status' => 'INVALID_PAYLOAD', 'message' => 'Valid JSON payload required']);
            exit;
        }

        $header = $payload['header'];
        $metrics = $payload['metrics'] ?? [];

        $deviceId = $header['device_id'];
        $fwVersion = $header['firmware_version'] ?? '1.0.0';
        $seqNo = (int)($header['seq_no'] ?? 0);
        $ts = (int)($header['timestamp'] ?? time());
        $temp = (float)($metrics['temperature'] ?? 0.0);
        $humi = (float)($metrics['humidity'] ?? 0.0);
        $battV = (float)($metrics['battery_voltage'] ?? 0.0);
        $battPct = (int)($metrics['battery_level_pct'] ?? 100);
        $rssi = (int)($metrics['rssi'] ?? 0);
        $interval = (int)($metrics['interval_sec'] ?? 15);

        $nextSleepSec = $interval;

        // テレメトリ保存
        $stmt = $pdo->prepare("INSERT INTO telemetry (device_id, seq_no, timestamp, temperature, humidity, battery_voltage, battery_level_pct, rssi, interval_sec, raw_json) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
        $stmt->execute([$deviceId, $seqNo, $ts, $temp, $humi, $battV, $battPct, $rssi, $interval, $rawInput]);

        // デバイス存在確認 & 更新
        $chkStmt = $pdo->prepare("SELECT total_telemetries, current_interval_sec FROM devices WHERE device_id = ?");
        $chkStmt->execute([$deviceId]);
        $devRow = $chkStmt->fetch();

        if ($devRow) {
            $total = (int)$devRow['total_telemetries'] + 1;
            $savedInterval = (int)$devRow['current_interval_sec'];
            if ($savedInterval > 0) $nextSleepSec = $savedInterval;

            $upStmt = $pdo->prepare("UPDATE devices SET firmware_version = ?, status = 'ONLINE', last_seen = ?, total_telemetries = ? WHERE device_id = ?");
            $upStmt->execute([$fwVersion, $ts, $total, $deviceId]);
        } else {
            $inStmt = $pdo->prepare("INSERT INTO devices (device_id, device_type, firmware_version, status, last_seen, registered_at, total_telemetries, current_interval_sec) VALUES (?, 'ESP32-S3', ?, 'ONLINE', ?, ?, 1, ?)");
            $inStmt->execute([$deviceId, $fwVersion, $ts, $ts, $interval]);
        }

        // 保留中コマンド取得
        $cmdStmt = $pdo->prepare("SELECT * FROM commands WHERE device_id = ? AND status = 'PENDING' ORDER BY created_at ASC");
        $cmdStmt->execute([$deviceId]);
        $pendingRows = $cmdStmt->fetchAll();

        $pendingCmds = [];
        foreach ($pendingRows as $row) {
            $params = json_decode($row['params_json'], true);
            $pendingCmds[] = [
                'command_id' => $row['command_id'],
                'action' => $row['action'],
                'params' => $params
            ];
            if ($row['action'] === 'CONFIG_UPDATE' && isset($params['sleep_interval_sec'])) {
                $nextSleepSec = (int)$params['sleep_interval_sec'];
                $upStmt = $pdo->prepare("UPDATE devices SET current_interval_sec = ? WHERE device_id = ?");
                $upStmt->execute([$nextSleepSec, $deviceId]);
            }
        }

        header('Content-Type: application/json; charset=utf-8');
        echo json_encode([
            'status' => 'OK',
            'message' => 'Telemetry accepted',
            'server_time' => time(),
            'sleep_interval_sec' => $nextSleepSec,
            'ota' => ['available' => false],
            'commands' => $pendingCmds
        ]);
        exit;
    }

    // 3. デバイス一覧取得: GET /api/v1/devices
    if ($endpoint === 'devices' && $method === 'GET') {
        $stmt = $pdo->query("SELECT * FROM devices ORDER BY last_seen DESC");
        $devices = $stmt->fetchAll();

        $result = [];
        $now = time();
        foreach ($devices as $d) {
            $cmdStmt = $pdo->prepare("SELECT COUNT(*) FROM commands WHERE device_id = ? AND status = 'PENDING'");
            $cmdStmt->execute([$d['device_id']]);
            $pendingCount = (int)$cmdStmt->fetchColumn();

            $status = ($now - (int)$d['last_seen'] < 180) ? 'ONLINE' : 'OFFLINE';

            $result[] = [
                'device_id' => $d['device_id'],
                'device_type' => $d['device_type'],
                'firmware_version' => $d['firmware_version'],
                'status' => $status,
                'last_seen' => (int)$d['last_seen'],
                'registered_at' => (int)$d['registered_at'],
                'total_telemetries' => (int)$d['total_telemetries'],
                'current_interval_sec' => (int)$d['current_interval_sec'],
                'pending_commands_count' => $pendingCount
            ];
        }

        header('Content-Type: application/json; charset=utf-8');
        echo json_encode($result);
        exit;
    }

    // 4. テレメトリ履歴取得: GET /api/v1/telemetry/history
    if ($endpoint === 'telemetry/history' && $method === 'GET') {
        $deviceId = $_GET['device_id'] ?? '';
        $limit = isset($_GET['limit']) ? (int)$_GET['limit'] : 20;

        if (empty($deviceId)) {
            http_response_code(400);
            header('Content-Type: application/json; charset=utf-8');
            echo json_encode(['status' => 'INVALID_PARAM', 'message' => 'device_id query parameter is required']);
            exit;
        }

        $stmt = $pdo->prepare("SELECT * FROM telemetry WHERE device_id = ? ORDER BY timestamp DESC LIMIT ?");
        $stmt->execute([$deviceId, $limit]);
        $rows = $stmt->fetchAll();

        $history = [];
        foreach ($rows as $r) {
            $history[] = [
                'header' => [
                    'device_id' => $r['device_id'],
                    'seq_no' => (int)$r['seq_no'],
                    'timestamp' => (int)$r['timestamp'],
                    'firmware_version' => '1.0.0'
                ],
                'metrics' => [
                    'temperature' => (float)$r['temperature'],
                    'humidity' => (float)$r['humidity'],
                    'battery_voltage' => (float)$r['battery_voltage'],
                    'battery_level_pct' => (int)$r['battery_level_pct'],
                    'rssi' => (int)$r['rssi'],
                    'interval_sec' => (int)$r['interval_sec']
                ]
            ];
        }

        header('Content-Type: application/json; charset=utf-8');
        echo json_encode($history);
        exit;
    }

    // 5. リモートコマンド発行: POST /api/v1/commands
    if ($endpoint === 'commands' && $method === 'POST') {
        $rawInput = file_get_contents('php://input');
        $req = json_decode($rawInput, true);

        if (!$req || empty($req['device_id']) || empty($req['action'])) {
            http_response_code(400);
            header('Content-Type: application/json; charset=utf-8');
            echo json_encode(['status' => 'INVALID_PARAM', 'message' => 'device_id and action required']);
            exit;
        }

        $cmdId = 'cmd-' . round(microtime(true) * 1000);
        $paramsJson = json_encode($req['params'] ?? []);

        $stmt = $pdo->prepare("INSERT INTO commands (command_id, device_id, action, params_json, status, created_at) VALUES (?, ?, ?, ?, 'PENDING', ?)");
        $stmt->execute([$cmdId, $req['device_id'], $req['action'], $paramsJson, time()]);

        header('Content-Type: application/json; charset=utf-8');
        echo json_encode([
            'status' => 'QUEUED',
            'command_id' => $cmdId,
            'device_id' => $req['device_id'],
            'action' => $req['action']
        ]);
        exit;
    }

    // 6. コマンド ACK 受領: POST /api/v1/commands/ack
    if ($endpoint === 'commands/ack' && $method === 'POST') {
        $rawInput = file_get_contents('php://input');
        $req = json_decode($rawInput, true);

        if ($req && !empty($req['device_id']) && !empty($req['command_id'])) {
            $stmt = $pdo->prepare("UPDATE commands SET status = 'ACKED', acked_at = ?, result = ? WHERE command_id = ? AND device_id = ?");
            $stmt->execute([time(), $req['result'] ?? 'SUCCESS', $req['command_id'], $req['device_id']]);
        }

        header('Content-Type: application/json; charset=utf-8');
        echo json_encode(['status' => 'ACK_RECORDED']);
        exit;
    }

    // 該当なし
    http_response_code(404);
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode(['status' => 'NOT_FOUND', 'message' => 'Unknown endpoint: ' . $endpoint]);

} catch (Throwable $e) {
    http_response_code(500);
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode([
        'status' => 'SERVER_ERROR',
        'error_class' => get_class($e),
        'message' => $e->getMessage(),
        'file' => basename($e->getFile()),
        'line' => $e->getLine()
    ]);
}
