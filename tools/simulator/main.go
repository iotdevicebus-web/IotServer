package main

import (
	"bytes"
	"context"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"math/rand"
	"net/http"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"
)

type MessageHeader struct {
	DeviceID        string `json:"device_id"`
	Timestamp       int64  `json:"timestamp"`
	SeqNo           uint32 `json:"seq_no"`
	FirmwareVersion string `json:"firmware_version"`
	BootCount       uint32 `json:"boot_count"`
}

type MetricsData struct {
	Temperature     float64 `json:"temperature"`
	Humidity        float64 `json:"humidity"`
	BatteryVoltage  float64 `json:"battery_voltage"`
	BatteryLevelPct uint32  `json:"battery_level_pct"`
	RSSI            int32   `json:"rssi"`
}

type DeviceStatus struct {
	State         string `json:"state"`
	UptimeSec     uint32 `json:"uptime_sec"`
	FreeHeapBytes uint32 `json:"free_heap_bytes"`
}

type TelemetryPayload struct {
	Header  MessageHeader `json:"header"`
	Metrics MetricsData   `json:"metrics"`
	Status  DeviceStatus  `json:"status"`
}

type OtaInfo struct {
	Available     bool   `json:"available"`
	TargetVersion string `json:"target_version,omitempty"`
	PackageURL    string `json:"package_url,omitempty"`
	SHA256        string `json:"sha256,omitempty"`
	SizeBytes     uint32 `json:"size_bytes,omitempty"`
	Mandatory     bool   `json:"mandatory,omitempty"`
}

type DeviceCommand struct {
	CommandID string                 `json:"command_id"`
	Action    string                 `json:"action"`
	Params    map[string]interface{} `json:"params"`
}

type ApiResponse struct {
	Status           string          `json:"status"`
	Message          string          `json:"message"`
	ServerTime       int64           `json:"server_time"`
	SleepIntervalSec uint32          `json:"sleep_interval_sec,omitempty"`
	OTA              *OtaInfo        `json:"ota,omitempty"`
	Commands         []DeviceCommand `json:"commands,omitempty"`
}

type SimulatedDevice struct {
	DeviceID        string
	FirmwareVersion string
	CertFile        string
	KeyFile         string
	Client          *http.Client
	ServerURL       string
	SeqNo           uint32
	BootCount       uint32
	BaseTemp        float64
	BaseHumidity    float64
	BatteryMv       uint32
}

func NewSimulatedDevice(deviceID, certFile, keyFile, caFile, serverURL string) (*SimulatedDevice, error) {
	cert, err := tls.LoadX509KeyPair(certFile, keyFile)
	if err != nil {
		return nil, fmt.Errorf("failed to load client cert for %s: %w", deviceID, err)
	}

	caPEM, err := os.ReadFile(caFile)
	if err != nil {
		return nil, fmt.Errorf("failed to read root CA cert: %w", err)
	}
	caPool := x509.NewCertPool()
	caPool.AppendCertsFromPEM(caPEM)

	tlsConfig := &tls.Config{
		Certificates: []tls.Certificate{cert},
		RootCAs:      caPool,
	}

	client := &http.Client{
		Timeout: 5 * time.Second,
		Transport: &http.Transport{
			TLSClientConfig: tlsConfig,
		},
	}

	return &SimulatedDevice{
		DeviceID:        deviceID,
		FirmwareVersion: "1.0.0",
		CertFile:        certFile,
		KeyFile:         keyFile,
		Client:          client,
		ServerURL:       serverURL,
		SeqNo:           1,
		BootCount:       1,
		BaseTemp:        22.0 + rand.Float64()*8.0,
		BaseHumidity:    45.0 + rand.Float64()*20.0,
		BatteryMv:       4100 - uint32(rand.Intn(300)),
	}, nil
}

func (d *SimulatedDevice) ExecuteCommand(cmd DeviceCommand) {
	log.Printf("[%s] >>> [COMMAND RECEIVED] Action: %s, ID: %s, Params: %+v", d.DeviceID, cmd.Action, cmd.CommandID, cmd.Params)

	result := "SUCCESS"
	switch cmd.Action {
	case "CONFIG_UPDATE":
		if interval, ok := cmd.Params["sleep_interval_sec"].(float64); ok {
			log.Printf("[%s] [CONFIG APPLIED] Sleep Interval updated to %ds", d.DeviceID, int(interval))
		}
	case "REBOOT":
		log.Printf("[%s] [SYSTEM] Forced Rebooting Device...", d.DeviceID)
		d.BootCount++
	case "SELF_TEST":
		log.Printf("[%s] [DIAGNOSTICS] Running Self-Test: Sensor=PASS, ADC=PASS, Flash=PASS", d.DeviceID)
	default:
		log.Printf("[%s] [WARN] Unknown Command Action: %s", d.DeviceID, cmd.Action)
		result = "UNKNOWN_ACTION"
	}

	// ACK 送信
	ackPayload := map[string]string{
		"device_id":  d.DeviceID,
		"command_id": cmd.CommandID,
		"result":     result,
	}
	ackData, _ := json.Marshal(ackPayload)
	req, _ := http.NewRequest("POST", d.ServerURL+"/api/v1/commands/ack", bytes.NewReader(ackData))
	req.Header.Set("Content-Type", "application/json")
	if resp, err := d.Client.Do(req); err == nil {
		resp.Body.Close()
		log.Printf("[%s] >>> [COMMAND ACK SENT] Result: %s for Command: %s", d.DeviceID, result, cmd.CommandID)
	}
}

func (d *SimulatedDevice) PerformOTA(ota *OtaInfo) bool {
	log.Printf("[%s] >>> [OTA START] Found new firmware: %s at %s", d.DeviceID, ota.TargetVersion, ota.PackageURL)
	start := time.Now()

	resp, err := d.Client.Get(ota.PackageURL)
	if err != nil {
		log.Printf("[%s] [OTA ERROR] Download failed: %v", d.DeviceID, err)
		return false
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		log.Printf("[%s] [OTA ERROR] Download returned status: %d", d.DeviceID, resp.StatusCode)
		return false
	}

	binData, err := io.ReadAll(resp.Body)
	if err != nil {
		log.Printf("[%s] [OTA ERROR] Failed to read binary data: %v", d.DeviceID, err)
		return false
	}

	// SHA-256 検証
	hash := sha256.Sum256(binData)
	computedHash := hex.EncodeToString(hash[:])

	log.Printf("[%s] [OTA INTEGRITY] Downloaded %d bytes in %v. SHA256: %s",
		d.DeviceID, len(binData), time.Since(start), computedHash)

	if ota.SHA256 != "" && computedHash != ota.SHA256 {
		log.Printf("[%s] [OTA ERROR] SHA256 Mismatch! Expected: %s, Got: %s", d.DeviceID, ota.SHA256, computedHash)
		return false
	}

	log.Printf("[%s] [OTA FLASH] Writing to Secondary Partition & Setting Boot Flags... OK!", d.DeviceID)
	log.Printf("[%s] >>> [OTA REBOOT] Restarting into firmware version %s <<<", d.DeviceID, ota.TargetVersion)

	// ファームウェアバージョン更新とリブートシミュレーション
	d.FirmwareVersion = ota.TargetVersion
	d.BootCount++
	return true
}

func (d *SimulatedDevice) Run(ctx context.Context, defaultIntervalSec int, maxCycles int, wg *sync.WaitGroup) {
	defer wg.Done()
	log.Printf("[%s] Device Simulator Started (FW: %s). Target: %s", d.DeviceID, d.FirmwareVersion, d.ServerURL)

	cycles := 0
	for {
		select {
		case <-ctx.Done():
			log.Printf("[%s] Simulator stopping on context done.", d.DeviceID)
			return
		default:
		}

		temp := d.BaseTemp + (rand.Float64()*2.0 - 1.0)

		humidity := d.BaseHumidity + (rand.Float64()*4.0 - 2.0)
		if d.BatteryMv > 3300 {
			d.BatteryMv -= 2
		}
		batteryVolt := float64(d.BatteryMv) / 1000.0
		batteryPct := uint32(0)
		if d.BatteryMv > 3300 {
			batteryPct = (d.BatteryMv - 3300) * 100 / 800
			if batteryPct > 100 {
				batteryPct = 100
			}
		}

		payload := TelemetryPayload{
			Header: MessageHeader{
				DeviceID:        d.DeviceID,
				Timestamp:       time.Now().UTC().Unix(),
				SeqNo:           d.SeqNo,
				FirmwareVersion: d.FirmwareVersion,
				BootCount:       d.BootCount,
			},
			Metrics: MetricsData{
				Temperature:     temp,
				Humidity:        humidity,
				BatteryVoltage:  batteryVolt,
				BatteryLevelPct: batteryPct,
				RSSI:            -55 - int32(rand.Intn(25)),
			},
			Status: DeviceStatus{
				State:         "NORMAL",
				UptimeSec:     uint32(cycles * defaultIntervalSec),
				FreeHeapBytes: 45000 + uint32(rand.Intn(5000)),
			},
		}
		d.SeqNo++

		data, _ := json.Marshal(payload)
		url := fmt.Sprintf("%s/api/v1/telemetry", d.ServerURL)

		start := time.Now()
		var resp *http.Response
		var err error
		for attempt := 0; attempt < 3; attempt++ {
			resp, err = d.Client.Post(url, "application/json", bytes.NewReader(data))
			if err == nil {
				break
			}
			time.Sleep(200 * time.Millisecond)
		}
		latency := time.Since(start)


		sleepSec := defaultIntervalSec
		if err != nil {
			log.Printf("[%s] [ERROR] POST failed: %v", d.DeviceID, err)
		} else {
			body, _ := io.ReadAll(resp.Body)
			resp.Body.Close()

			if resp.StatusCode == http.StatusOK {
				cycles++
				var apiResp ApiResponse
				if err := json.Unmarshal(body, &apiResp); err == nil {
					log.Printf("[%s] [OK 200] Seq=%d, FW=%s, Temp=%.2fC, Batt=%.2fV (%d%%), Latency=%v, SleepFor=%ds",
						d.DeviceID, payload.Header.SeqNo, d.FirmwareVersion, temp, batteryVolt, batteryPct, latency, defaultIntervalSec)

					// OTA 通知の処理
					if apiResp.OTA != nil && apiResp.OTA.Available && apiResp.OTA.TargetVersion != d.FirmwareVersion {
						d.PerformOTA(apiResp.OTA)
					}

					// リモートコマンドの実行 & ACK
					if len(apiResp.Commands) > 0 {
						for _, cmd := range apiResp.Commands {
							d.ExecuteCommand(cmd)
						}
					}

				}
			} else {
				log.Printf("[%s] [HTTP %d] Response: %s", d.DeviceID, resp.StatusCode, string(body))
				time.Sleep(500 * time.Millisecond)
			}
		}


		if maxCycles > 0 && cycles >= maxCycles {
			log.Printf("[%s] Completed %d cycles. Exiting.", d.DeviceID, cycles)
			return
		}

		select {
		case <-ctx.Done():
			return
		case <-time.After(time.Duration(sleepSec) * time.Second):
		}
	}
}

func main() {
	serverURL := flag.String("server", "https://127.0.0.1:8443", "Target server HTTPS URL")
	caCert := flag.String("ca", "../certs/out/ca.crt", "Root CA certificate path")
	clientCert := flag.String("cert", "../certs/out/client_DEV-ESP32-001.crt", "Client certificate path")
	clientKey := flag.String("key", "../certs/out/client_DEV-ESP32-001.key", "Client private key path")
	deviceID := flag.String("id", "DEV-ESP32-001", "Device ID")
	interval := flag.Int("interval", 1, "Transmission interval in seconds")
	cycles := flag.Int("cycles", 3, "Number of cycles to run (0 for infinite)")
	flag.Parse()

	log.Println("====================================================")
	log.Printf("   Starting Edge Simulator for %s", *deviceID)
	log.Println("====================================================")

	device, err := NewSimulatedDevice(*deviceID, *clientCert, *clientKey, *caCert, *serverURL)
	if err != nil {
		log.Fatalf("Failed to initialize simulated device: %v", err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	stopChan := make(chan os.Signal, 1)
	signal.Notify(stopChan, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-stopChan
		log.Println("Stopping simulator...")
		cancel()
	}()

	var wg sync.WaitGroup
	wg.Add(1)
	go device.Run(ctx, *interval, *cycles, &wg)

	wg.Wait()
	log.Println("Simulator execution finished.")
}
