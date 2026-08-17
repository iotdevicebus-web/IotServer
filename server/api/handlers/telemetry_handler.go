package handlers

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"iot-platform-server/api/middleware"
	"iot-platform-server/core/alerting"
	"iot-platform-server/core/telemetry"
	"iot-platform-server/storage"
	"iot-platform-server/storage/models"
	"log"
	"net/http"
	"strings"
	"sync"
	"time"
)


type ApiHandler struct {
	repo         storage.Repository
	otaMu        sync.RWMutex
	availableOTA *models.OtaInfo
	ruleEngine   *alerting.RuleEngine
}

func NewApiHandler(repo storage.Repository, webhookURL string) *ApiHandler {
	dummyBin := []byte("FIRMWARE_IMAGE_V1.1.0_PROD_RELEASE_DATA_2026")
	hash := sha256.Sum256(dummyBin)
	shaHex := hex.EncodeToString(hash[:])

	return &ApiHandler{
		repo: repo,
		availableOTA: &models.OtaInfo{
			Available:     true,
			TargetVersion: "1.1.0",
			PackageURL:    "https://127.0.0.1:8443/api/v1/ota/download/1.1.0",
			SHA256:        shaHex,
			SizeBytes:     uint32(len(dummyBin) * 1024),
			Mandatory:     false,
		},
		ruleEngine: alerting.NewRuleEngine(alerting.DefaultThresholds(), webhookURL, repo),
	}
}

// HandleTelemetry POST /api/v1/telemetry
func (h *ApiHandler) HandleTelemetry(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method Not Allowed", http.StatusMethodNotAllowed)
		return
	}

	authDeviceID, _ := middleware.GetAuthenticatedDeviceID(r.Context())

	body, err := io.ReadAll(r.Body)
	if err != nil {
		writeError(w, http.StatusBadRequest, "INVALID_PAYLOAD", "Failed to read request body")
		return
	}
	defer r.Body.Close()

	var payload models.TelemetryPayload
	contentType := r.Header.Get("Content-Type")

	if strings.Contains(contentType, "protobuf") || strings.Contains(contentType, "octet-stream") {
		// Protocol Buffers デコード
		pbPayload, err := telemetry.DecodeTelemetryProtobuf(body)
		if err != nil {
			writeError(w, http.StatusBadRequest, "INVALID_PAYLOAD", fmt.Sprintf("Protobuf decode error: %v", err))
			return
		}
		payload = *pbPayload
		log.Printf("[TELEMETRY PROTOBUF] Decoded Protobuf payload (%d bytes)", len(body))
	} else {
		// JSON デコード
		if err := json.Unmarshal(body, &payload); err != nil {
			writeError(w, http.StatusBadRequest, "INVALID_PAYLOAD", fmt.Sprintf("JSON parse error: %v", err))
			return
		}
	}


	// 証明書IDとペイロード内device_idの偽装検証
	if authDeviceID != "" && payload.Header.DeviceID != authDeviceID {
		log.Printf("[SECURITY ALERT] Device ID mismatch! Auth Cert: %s, Payload: %s", authDeviceID, payload.Header.DeviceID)
		writeError(w, http.StatusForbidden, "UNAUTHORIZED", "Device ID in payload does not match client certificate")
		return
	}

	// 1. テレメトリの永続化
	if err := h.repo.SaveTelemetry(&payload); err != nil {
		log.Printf("[ERROR] Failed to save telemetry: %v", err)
		writeError(w, http.StatusInternalServerError, "STORAGE_ERROR", "Internal storage error")
		return
	}

	// 2. リアルタイム異常検知ルールエンジンの評価
	h.ruleEngine.EvaluateAndAlert(&payload)

	// 3. ログ出力
	log.Printf("[TELEMETRY] Device=%s, FW=%s: Temp=%.2fC, Batt=%.2fV (%d%%), RSSI=%ddBm, Seq=%d",
		payload.Header.DeviceID,
		payload.Header.FirmwareVersion,
		payload.Metrics.Temperature,
		payload.Metrics.BatteryVoltage,
		payload.Metrics.BatteryLevelPct,
		payload.Metrics.RSSI,
		payload.Header.SeqNo,
	)


	// OTA 判定: デバイスのバージョンが 1.1.0 未満であれば OTA 情報をレスポンスにピギーバック
	// OTA 判定
	var otaInfo *models.OtaInfo
	h.otaMu.RLock()
	if h.availableOTA != nil && payload.Header.FirmwareVersion != h.availableOTA.TargetVersion {
		otaInfo = h.availableOTA
		log.Printf("[OTA NOTIFY] Announcing OTA %s to device %s (current: %s)",
			h.availableOTA.TargetVersion, payload.Header.DeviceID, payload.Header.FirmwareVersion)
	} else {
		otaInfo = &models.OtaInfo{Available: false}
	}
	h.otaMu.RUnlock()

	// 3. 次回スリープ間隔の決定 (動的制御)
	sleepIntervalSec := uint32(60)
	if payload.Metrics.BatteryLevelPct < 20 {
		sleepIntervalSec = 300 // バッテリ低下時は間隔延長
	}

	// 4. 保留中リモートコマンドの取得
	pendingCmds, _ := h.repo.GetPendingCommands(payload.Header.DeviceID)
	if len(pendingCmds) > 0 {
		log.Printf("[COMMAND DISPATCH] Piggybacking %d remote commands to device %s", len(pendingCmds), payload.Header.DeviceID)
	}

	// 5. レスポンス返却
	resp := models.ApiResponse{
		Status:           "OK",
		Message:          "Telemetry accepted",
		ServerTime:       time.Now().UTC().Unix(),
		SleepIntervalSec: sleepIntervalSec,
		OTA:              otaInfo,
		Commands:         pendingCmds,
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(resp)
}

// HandleQueueCommand POST /api/v1/commands
func (h *ApiHandler) HandleQueueCommand(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeError(w, http.StatusMethodNotAllowed, "METHOD_NOT_ALLOWED", "Only POST allowed")
		return
	}

	var req struct {
		DeviceID string                 `json:"device_id"`
		Action   string                 `json:"action"`
		Params   map[string]interface{} `json:"params"`
	}

	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.DeviceID == "" || req.Action == "" {
		writeError(w, http.StatusBadRequest, "INVALID_PARAM", "Invalid command payload")
		return
	}

	cmdID := fmt.Sprintf("cmd-%d", time.Now().UnixNano())
	cmd := &models.DeviceCommand{
		CommandID: cmdID,
		Action:    req.Action,
		Params:    req.Params,
	}

	if err := h.repo.QueueCommand(req.DeviceID, cmd); err != nil {
		writeError(w, http.StatusInternalServerError, "ERROR", fmt.Sprintf("Failed to queue command: %v", err))
		return
	}

	log.Printf("[COMMAND QUEUED] Command %s (%s) queued for device %s", cmdID, req.Action, req.DeviceID)
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]interface{}{
		"status":     "QUEUED",
		"command_id": cmdID,
		"device_id":  req.DeviceID,
		"action":     req.Action,
	})
}

// HandleAckCommand POST /api/v1/commands/ack
func (h *ApiHandler) HandleAckCommand(w http.ResponseWriter, r *http.Request) {
	var req struct {
		DeviceID  string `json:"device_id"`
		CommandID string `json:"command_id"`
		Result    string `json:"result"`
	}
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeError(w, http.StatusBadRequest, "INVALID_PARAM", "Invalid ACK payload")
		return
	}

	_ = h.repo.AckCommand(req.DeviceID, req.CommandID, req.Result)
	log.Printf("[COMMAND ACKED] Device %s confirmed execution of command %s: %s", req.DeviceID, req.CommandID, req.Result)
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{"status": "ACK_RECORDED"})
}

// HandleOtaDownload GET /api/v1/ota/download/{version}
func (h *ApiHandler) HandleOtaDownload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "Method Not Allowed", http.StatusMethodNotAllowed)
		return
	}

	authDeviceID, _ := middleware.GetAuthenticatedDeviceID(r.Context())
	parts := strings.Split(r.URL.Path, "/")
	version := parts[len(parts)-1]

	log.Printf("[OTA DOWNLOAD] Device %s requested binary for version %s", authDeviceID, version)

	// ダミーバイナリデータをストリーム返却
	data := []byte("FIRMWARE_IMAGE_V1.1.0_PROD_RELEASE_DATA_2026")
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Disposition", fmt.Sprintf("attachment; filename=firmware_%s.bin", version))
	w.Header().Set("Content-Length", fmt.Sprintf("%d", len(data)))
	w.WriteHeader(http.StatusOK)
	w.Write(data)
}

// HandleEvents POST /api/v1/events
func (h *ApiHandler) HandleEvents(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "Method Not Allowed", http.StatusMethodNotAllowed)
		return
	}

	body, err := io.ReadAll(r.Body)
	if err != nil {
		writeError(w, http.StatusBadRequest, "INVALID_PAYLOAD", "Failed to read request body")
		return
	}
	defer r.Body.Close()

	var payload models.EventPayload
	if err := json.Unmarshal(body, &payload); err != nil {
		writeError(w, http.StatusBadRequest, "INVALID_PAYLOAD", fmt.Sprintf("JSON parse error: %v", err))
		return
	}

	if err := h.repo.SaveEvent(&payload); err != nil {
		writeError(w, http.StatusInternalServerError, "ERROR", "Failed to save event")
		return
	}

	log.Printf("[EVENT ALERT] Device: %s, Type: %s, Severity: %s, Message: %s",
		payload.Header.DeviceID,
		payload.Event.EventType,
		payload.Event.Severity,
		payload.Event.Message,
	)

	resp := models.ApiResponse{
		Status:     "OK",
		Message:    "Event recorded",
		ServerTime: time.Now().UTC().Unix(),
	}

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	json.NewEncoder(w).Encode(resp)
}

// HandleListDevices GET /api/v1/devices
func (h *ApiHandler) HandleListDevices(w http.ResponseWriter, r *http.Request) {
	devices := h.repo.ListDevices()
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(devices)
}

// HandleTelemetryHistory GET /api/v1/telemetry/history?device_id=DEV-ESP32-001&limit=20
func (h *ApiHandler) HandleTelemetryHistory(w http.ResponseWriter, r *http.Request) {
	deviceID := r.URL.Query().Get("device_id")
	if deviceID == "" {
		writeError(w, http.StatusBadRequest, "INVALID_PARAM", "device_id query parameter is required")
		return
	}

	history, err := h.repo.GetTelemetryHistory(deviceID, 50)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "ERROR", fmt.Sprintf("Failed to query history: %v", err))
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(history)
}

// HandleHealth GET /healthz
func (h *ApiHandler) HandleHealth(w http.ResponseWriter, r *http.Request) {

	w.Header().Set("Content-Type", "application/json")
	w.Write([]byte(`{"status":"HEALTHY","timestamp":` + fmt.Sprintf("%d", time.Now().UTC().Unix()) + `}`))
}

func writeError(w http.ResponseWriter, statusCode int, status, msg string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	json.NewEncoder(w).Encode(models.ApiResponse{
		Status:     status,
		Message:    msg,
		ServerTime: time.Now().UTC().Unix(),
	})
}
