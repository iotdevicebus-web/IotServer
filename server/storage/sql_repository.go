package storage

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"iot-platform-server/storage/models"
	"log"
	"time"

	_ "modernc.org/sqlite"
)

type SQLRepository struct {
	db *sql.DB
}

func NewSQLRepository(driverName, dsn string) (*SQLRepository, error) {
	db, err := sql.Open(driverName, dsn)
	if err != nil {
		return nil, fmt.Errorf("failed to open database: %w", err)
	}

	// SQLite の並行書き込みロック競合を防ぐための設定
	if driverName == "sqlite" {
		db.SetMaxOpenConns(1) // SQLiteファイル書き込みロック競合防止
		_, _ = db.Exec("PRAGMA journal_mode = WAL;")
		_, _ = db.Exec("PRAGMA busy_timeout = 5000;")
		_, _ = db.Exec("PRAGMA synchronous = NORMAL;")
	} else {
		db.SetMaxOpenConns(25)
		db.SetMaxIdleConns(5)
	}
	db.SetConnMaxLifetime(5 * time.Minute)

	repo := &SQLRepository{db: db}
	if err := repo.migrate(); err != nil {
		db.Close()
		return nil, fmt.Errorf("database migration failed: %w", err)
	}

	log.Printf("[STORAGE] Connected to %s database at: %s (WAL mode & busy_timeout configured)", driverName, dsn)
	return repo, nil
}


func (r *SQLRepository) migrate() error {
	schema := `
	CREATE TABLE IF NOT EXISTS devices (
		device_id TEXT PRIMARY KEY,
		firmware_version TEXT NOT NULL,
		status TEXT NOT NULL,
		last_seq_no INTEGER NOT NULL,
		last_seen_at DATETIME NOT NULL,
		total_telemetries INTEGER NOT NULL DEFAULT 0,
		last_temp REAL,
		last_humidity REAL,
		last_voltage REAL,
		last_battery_pct INTEGER,
		last_rssi INTEGER,
		created_at DATETIME NOT NULL
	);

	CREATE TABLE IF NOT EXISTS telemetries (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		device_id TEXT NOT NULL,
		timestamp INTEGER NOT NULL,
		seq_no INTEGER NOT NULL,
		firmware_version TEXT NOT NULL,
		boot_count INTEGER,
		temperature REAL,
		humidity REAL,
		battery_voltage REAL,
		battery_level_pct INTEGER,
		rssi INTEGER,
		state TEXT,
		uptime_sec INTEGER,
		free_heap_bytes INTEGER,
		custom_values_json TEXT,
		created_at DATETIME NOT NULL
	);

	CREATE INDEX IF NOT EXISTS idx_telemetries_device_ts ON telemetries(device_id, timestamp DESC);

	CREATE TABLE IF NOT EXISTS events (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		device_id TEXT NOT NULL,
		timestamp INTEGER NOT NULL,
		event_type TEXT NOT NULL,
		severity TEXT NOT NULL,
		message TEXT NOT NULL,
		details_json TEXT,
		created_at DATETIME NOT NULL
	);

	CREATE INDEX IF NOT EXISTS idx_events_device_ts ON events(device_id, timestamp DESC);

	CREATE TABLE IF NOT EXISTS commands (
		id INTEGER PRIMARY KEY AUTOINCREMENT,
		device_id TEXT NOT NULL,
		command_id TEXT NOT NULL UNIQUE,
		action TEXT NOT NULL,
		params_json TEXT,
		status TEXT NOT NULL DEFAULT 'PENDING',
		created_at DATETIME NOT NULL,
		sent_at DATETIME,
		acked_at DATETIME
	);

	CREATE INDEX IF NOT EXISTS idx_commands_device_status ON commands(device_id, status);
	`


	_, err := r.db.Exec(schema)
	return err
}

func (r *SQLRepository) SaveTelemetry(t *models.TelemetryPayload) error {
	tx, err := r.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()

	now := time.Now().UTC()

	// 1. telemetries テーブルへの時系列レコード挿入
	customJSON, _ := json.Marshal(t.Metrics.CustomValues)
	stateStr := "NORMAL"
	uptimeSec := uint32(0)
	freeHeap := uint32(0)
	if t.Status != nil {
		stateStr = t.Status.State
		uptimeSec = t.Status.UptimeSec
		freeHeap = t.Status.FreeHeapBytes
	}

	_, err = tx.Exec(`
		INSERT INTO telemetries (
			device_id, timestamp, seq_no, firmware_version, boot_count,
			temperature, humidity, battery_voltage, battery_level_pct, rssi,
			state, uptime_sec, free_heap_bytes, custom_values_json, created_at
		) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
	`,
		t.Header.DeviceID, t.Header.Timestamp, t.Header.SeqNo, t.Header.FirmwareVersion, t.Header.BootCount,
		t.Metrics.Temperature, t.Metrics.Humidity, t.Metrics.BatteryVoltage, t.Metrics.BatteryLevelPct, t.Metrics.RSSI,
		stateStr, uptimeSec, freeHeap, string(customJSON), now,
	)
	if err != nil {
		return fmt.Errorf("insert telemetry failed: %w", err)
	}

	// 2. devices テーブルの UPSERT (状態更新)
	_, err = tx.Exec(`
		INSERT INTO devices (
			device_id, firmware_version, status, last_seq_no, last_seen_at, total_telemetries,
			last_temp, last_humidity, last_voltage, last_battery_pct, last_rssi, created_at
		) VALUES (?, ?, 'ONLINE', ?, ?, 1, ?, ?, ?, ?, ?, ?)
		ON CONFLICT(device_id) DO UPDATE SET
			firmware_version = excluded.firmware_version,
			status = 'ONLINE',
			last_seq_no = excluded.last_seq_no,
			last_seen_at = excluded.last_seen_at,
			total_telemetries = devices.total_telemetries + 1,
			last_temp = excluded.last_temp,
			last_humidity = excluded.last_humidity,
			last_voltage = excluded.last_voltage,
			last_battery_pct = excluded.last_battery_pct,
			last_rssi = excluded.last_rssi
	`,
		t.Header.DeviceID, t.Header.FirmwareVersion, t.Header.SeqNo, now,
		t.Metrics.Temperature, t.Metrics.Humidity, t.Metrics.BatteryVoltage, t.Metrics.BatteryLevelPct, t.Metrics.RSSI, now,
	)
	if err != nil {
		return fmt.Errorf("upsert device failed: %w", err)
	}

	return tx.Commit()
}

func (r *SQLRepository) SaveEvent(e *models.EventPayload) error {
	detailsJSON, _ := json.Marshal(e.Event.Details)
	now := time.Now().UTC()

	_, err := r.db.Exec(`
		INSERT INTO events (device_id, timestamp, event_type, severity, message, details_json, created_at)
		VALUES (?, ?, ?, ?, ?, ?, ?)
	`,
		e.Header.DeviceID, e.Header.Timestamp, e.Event.EventType, e.Event.Severity, e.Event.Message, string(detailsJSON), now,
	)
	return err
}

func (r *SQLRepository) GetDevice(deviceID string) (*models.DeviceState, error) {
	row := r.db.QueryRow(`
		SELECT device_id, firmware_version, status, last_seq_no, last_seen_at, total_telemetries,
		       last_temp, last_humidity, last_voltage, last_battery_pct, last_rssi
		FROM devices WHERE device_id = ?
	`, deviceID)

	var d models.DeviceState
	var lastTemp, lastHumi, lastVolt sql.NullFloat64
	var lastBattPct, lastRSSI sql.NullInt64

	err := row.Scan(
		&d.DeviceID, &d.FirmwareVersion, &d.Status, &d.LastSeqNo, &d.LastSeenAt, &d.TotalTelemetries,
		&lastTemp, &lastHumi, &lastVolt, &lastBattPct, &lastRSSI,
	)
	if err == sql.ErrNoRows {
		return nil, ErrDeviceNotFound
	} else if err != nil {
		return nil, err
	}

	d.LastMetrics = models.MetricsData{
		Temperature:     lastTemp.Float64,
		Humidity:        lastHumi.Float64,
		BatteryVoltage:  lastVolt.Float64,
		BatteryLevelPct: uint32(lastBattPct.Int64),
		RSSI:            int32(lastRSSI.Int64),
	}

	return &d, nil
}

func (r *SQLRepository) ListDevices() []*models.DeviceState {
	rows, err := r.db.Query(`
		SELECT d.device_id, d.firmware_version, d.status, d.last_seq_no, d.last_seen_at, d.total_telemetries,
		       d.last_temp, d.last_humidity, d.last_voltage, d.last_battery_pct, d.last_rssi,
		       (SELECT COUNT(*) FROM commands c WHERE c.device_id = d.device_id AND c.status = 'PENDING') AS pending_count
		FROM devices d ORDER BY d.last_seen_at DESC
	`)
	if err != nil {
		log.Printf("[ERROR] ListDevices query failed: %v", err)
		return []*models.DeviceState{}
	}
	defer rows.Close()

	var list []*models.DeviceState
	for rows.Next() {
		var d models.DeviceState
		var lastTemp, lastHumi, lastVolt sql.NullFloat64
		var lastBattPct, lastRSSI sql.NullInt64
		var pendingCount int

		if err := rows.Scan(
			&d.DeviceID, &d.FirmwareVersion, &d.Status, &d.LastSeqNo, &d.LastSeenAt, &d.TotalTelemetries,
			&lastTemp, &lastHumi, &lastVolt, &lastBattPct, &lastRSSI,
			&pendingCount,
		); err != nil {
			continue
		}

		d.PendingCommandsCount = pendingCount
		d.LastMetrics = models.MetricsData{
			Temperature:     lastTemp.Float64,
			Humidity:        lastHumi.Float64,
			BatteryVoltage:  lastVolt.Float64,
			BatteryLevelPct: uint32(lastBattPct.Int64),
			RSSI:            int32(lastRSSI.Int64),
		}

		list = append(list, &d)
	}
	return list
}


func (r *SQLRepository) GetTelemetryHistory(deviceID string, limit int) ([]models.TelemetryPayload, error) {
	if limit <= 0 {
		limit = 50
	}
	rows, err := r.db.Query(`
		SELECT timestamp, seq_no, firmware_version, boot_count,
		       temperature, humidity, battery_voltage, battery_level_pct, rssi,
		       state, uptime_sec, free_heap_bytes
		FROM telemetries
		WHERE device_id = ?
		ORDER BY timestamp DESC
		LIMIT ?
	`, deviceID, limit)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var list []models.TelemetryPayload
	for rows.Next() {
		var t models.TelemetryPayload
		var bootCount, uptimeSec, freeHeap sql.NullInt64
		var state sql.NullString
		t.Header.DeviceID = deviceID

		err := rows.Scan(
			&t.Header.Timestamp, &t.Header.SeqNo, &t.Header.FirmwareVersion, &bootCount,
			&t.Metrics.Temperature, &t.Metrics.Humidity, &t.Metrics.BatteryVoltage, &t.Metrics.BatteryLevelPct, &t.Metrics.RSSI,
			&state, &uptimeSec, &freeHeap,
		)
		if err != nil {
			continue
		}
		t.Header.BootCount = uint32(bootCount.Int64)
		t.Status = &models.DeviceStatus{
			State:         state.String,
			UptimeSec:     uint32(uptimeSec.Int64),
			FreeHeapBytes: uint32(freeHeap.Int64),
		}
		list = append(list, t)
	}
	return list, nil
}

func (r *SQLRepository) QueueCommand(deviceID string, cmd *models.DeviceCommand) error {
	paramsJSON, _ := json.Marshal(cmd.Params)
	now := time.Now().UTC()

	_, err := r.db.Exec(`
		INSERT INTO commands (device_id, command_id, action, params_json, status, created_at)
		VALUES (?, ?, ?, ?, 'PENDING', ?)
	`, deviceID, cmd.CommandID, cmd.Action, string(paramsJSON), now)
	return err
}

func (r *SQLRepository) GetPendingCommands(deviceID string) ([]models.DeviceCommand, error) {
	rows, err := r.db.Query(`
		SELECT command_id, action, params_json
		FROM commands
		WHERE device_id = ? AND status = 'PENDING'
		ORDER BY created_at ASC
	`, deviceID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var list []models.DeviceCommand
	var cmdIDs []string

	for rows.Next() {
		var cmd models.DeviceCommand
		var paramsJSON string
		if err := rows.Scan(&cmd.CommandID, &cmd.Action, &paramsJSON); err != nil {
			continue
		}
		if paramsJSON != "" {
			_ = json.Unmarshal([]byte(paramsJSON), &cmd.Params)
		}
		list = append(list, cmd)
		cmdIDs = append(cmdIDs, cmd.CommandID)
	}

	// 抽出したコマンドのステータスを 'SENT' に更新
	if len(cmdIDs) > 0 {
		now := time.Now().UTC()
		for _, cid := range cmdIDs {
			_, _ = r.db.Exec(`UPDATE commands SET status = 'SENT', sent_at = ? WHERE command_id = ?`, now, cid)
		}
	}

	return list, nil
}

func (r *SQLRepository) AckCommand(deviceID, commandID, result string) error {
	now := time.Now().UTC()
	_, err := r.db.Exec(`
		UPDATE commands SET status = 'ACKED', acked_at = ? WHERE device_id = ? AND command_id = ?
	`, now, deviceID, commandID)
	return err
}

