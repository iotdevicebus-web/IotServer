package telemetry

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"iot-platform-server/storage/models"
	"math"
)

// DecodeTelemetryProtobuf Protocol Buffers v3 Wire format から TelemetryPayload をパース
func DecodeTelemetryProtobuf(data []byte) (*models.TelemetryPayload, error) {
	reader := bytes.NewReader(data)
	payload := &models.TelemetryPayload{
		Status: &models.DeviceStatus{},
	}

	for reader.Len() > 0 {
		tag, err := binary.ReadUvarint(reader)
		if err != nil {
			if err == io.EOF {
				break
			}
			return nil, err
		}

		fieldNum := tag >> 3
		wireType := tag & 0x7

		switch fieldNum {
		case 1: // header (length-delimited)
			length, err := binary.ReadUvarint(reader)
			if err != nil {
				return nil, err
			}
			subData := make([]byte, length)
			if _, err := io.ReadFull(reader, subData); err != nil {
				return nil, err
			}
			if err := parseHeader(subData, &payload.Header); err != nil {
				return nil, err
			}

		case 2: // metrics (length-delimited)
			length, err := binary.ReadUvarint(reader)
			if err != nil {
				return nil, err
			}
			subData := make([]byte, length)
			if _, err := io.ReadFull(reader, subData); err != nil {
				return nil, err
			}
			if err := parseMetrics(subData, &payload.Metrics); err != nil {
				return nil, err
			}

		case 3: // status (length-delimited)
			length, err := binary.ReadUvarint(reader)
			if err != nil {
				return nil, err
			}
			subData := make([]byte, length)
			if _, err := io.ReadFull(reader, subData); err != nil {
				return nil, err
			}
			if err := parseStatus(subData, payload.Status); err != nil {
				return nil, err
			}

		default:
			// 未知フィールドのスキップ
			if err := skipField(reader, wireType); err != nil {
				return nil, err
			}
		}
	}

	return payload, nil
}

func parseHeader(data []byte, h *models.MessageHeader) error {
	r := bytes.NewReader(data)
	for r.Len() > 0 {
		tag, err := binary.ReadUvarint(r)
		if err != nil {
			break
		}
		field := tag >> 3
		wire := tag & 0x7

		switch field {
		case 1: // device_id
			str, err := readString(r)
			if err != nil {
				return err
			}
			h.DeviceID = str
		case 2: // timestamp
			v, err := binary.ReadUvarint(r)
			if err != nil {
				return err
			}
			h.Timestamp = int64(v)
		case 3: // seq_no
			v, err := binary.ReadUvarint(r)
			if err != nil {
				return err
			}
			h.SeqNo = uint32(v)
		case 4: // firmware_version
			str, err := readString(r)
			if err != nil {
				return err
			}
			h.FirmwareVersion = str
		case 5: // boot_count
			v, err := binary.ReadUvarint(r)
			if err != nil {
				return err
			}
			h.BootCount = uint32(v)
		default:
			if err := skipField(r, wire); err != nil {
				return err
			}
		}
	}
	return nil
}

func parseMetrics(data []byte, m *models.MetricsData) error {
	r := bytes.NewReader(data)
	for r.Len() > 0 {
		tag, err := binary.ReadUvarint(r)
		if err != nil {
			break
		}
		field := tag >> 3
		wire := tag & 0x7

		switch field {
		case 1: // temperature (float32, wire type 5)
			var raw uint32
			if err := binary.Read(r, binary.LittleEndian, &raw); err != nil {
				return err
			}
			m.Temperature = float64(math.Float32frombits(raw))
		case 2: // humidity (float32)
			var raw uint32
			if err := binary.Read(r, binary.LittleEndian, &raw); err != nil {
				return err
			}
			m.Humidity = float64(math.Float32frombits(raw))
		case 3: // battery_voltage (float32)
			var raw uint32
			if err := binary.Read(r, binary.LittleEndian, &raw); err != nil {
				return err
			}
			m.BatteryVoltage = float64(math.Float32frombits(raw))
		case 4: // battery_level_pct (varint)
			v, err := binary.ReadUvarint(r)
			if err != nil {
				return err
			}
			m.BatteryLevelPct = uint32(v)
		case 5: // rssi (varint/int32)
			v, err := binary.ReadUvarint(r)
			if err != nil {
				return err
			}
			m.RSSI = int32(v)
		case 6: // interval_sec (varint/uint32)
			v, err := binary.ReadUvarint(r)
			if err != nil {
				return err
			}
			m.IntervalSec = uint32(v)

		default:
			if err := skipField(r, wire); err != nil {
				return err
			}
		}
	}
	return nil
}

func parseStatus(data []byte, s *models.DeviceStatus) error {
	r := bytes.NewReader(data)
	for r.Len() > 0 {
		tag, err := binary.ReadUvarint(r)
		if err != nil {
			break
		}
		field := tag >> 3
		wire := tag & 0x7

		switch field {
		case 1: // state enum
			v, err := binary.ReadUvarint(r)
			if err != nil {
				return err
			}
			switch v {
			case 1:
				s.State = "LOW_POWER"
			case 2:
				s.State = "WARNING"
			case 3:
				s.State = "ERROR"
			default:
				s.State = "NORMAL"
			}
		case 2: // uptime_sec
			v, err := binary.ReadUvarint(r)
			if err != nil {
				return err
			}
			s.UptimeSec = uint32(v)
		case 3: // free_heap_bytes
			v, err := binary.ReadUvarint(r)
			if err != nil {
				return err
			}
			s.FreeHeapBytes = uint32(v)
		default:
			if err := skipField(r, wire); err != nil {
				return err
			}
		}
	}
	return nil
}

func readString(r *bytes.Reader) (string, error) {
	length, err := binary.ReadUvarint(r)
	if err != nil {
		return "", err
	}
	buf := make([]byte, length)
	if _, err := io.ReadFull(r, buf); err != nil {
		return "", err
	}
	return string(buf), nil
}

func skipField(r *bytes.Reader, wireType uint64) error {
	switch wireType {
	case 0: // Varint
		_, err := binary.ReadUvarint(r)
		return err
	case 1: // 64-bit
		var raw uint64
		return binary.Read(r, binary.LittleEndian, &raw)
	case 2: // Length-delimited
		len, err := binary.ReadUvarint(r)
		if err != nil {
			return err
		}
		_, err = r.Seek(int64(len), io.SeekCurrent)
		return err
	case 5: // 32-bit
		var raw uint32
		return binary.Read(r, binary.LittleEndian, &raw)
	default:
		return fmt.Errorf("unsupported wire type: %d", wireType)
	}
}
