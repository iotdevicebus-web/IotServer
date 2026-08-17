package storage

import (
	"errors"
	"iot-platform-server/storage/models"
	"sync"
	"time"
)

var (
	ErrDeviceNotFound = errors.New("device not found")
)

type Repository interface {
	SaveTelemetry(telemetry *models.TelemetryPayload) error
	GetDevice(deviceID string) (*models.DeviceState, error)
	ListDevices() []*models.DeviceState
	SaveEvent(event *models.EventPayload) error
	GetTelemetryHistory(deviceID string, limit int) ([]models.TelemetryPayload, error)
	QueueCommand(deviceID string, cmd *models.DeviceCommand) error
	GetPendingCommands(deviceID string) ([]models.DeviceCommand, error)
	AckCommand(deviceID, commandID, result string) error
}



type InMemoryRepository struct {
	mu          sync.RWMutex
	devices     map[string]*models.DeviceState
	telemetries map[string][]models.TelemetryPayload
	events      map[string][]models.EventPayload
	commands    map[string][]*models.DeviceCommand
}

func NewInMemoryRepository() *InMemoryRepository {
	return &InMemoryRepository{
		devices:     make(map[string]*models.DeviceState),
		telemetries: make(map[string][]models.TelemetryPayload),
		events:      make(map[string][]models.EventPayload),
		commands:    make(map[string][]*models.DeviceCommand),
	}
}

func (r *InMemoryRepository) SaveTelemetry(t *models.TelemetryPayload) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	deviceID := t.Header.DeviceID
	dev, exists := r.devices[deviceID]
	if !exists {
		dev = &models.DeviceState{
			DeviceID: deviceID,
		}
		r.devices[deviceID] = dev
	}

	dev.FirmwareVersion = t.Header.FirmwareVersion
	dev.LastSeenAt = time.Now().UTC()
	dev.LastSeqNo = t.Header.SeqNo
	dev.LastMetrics = t.Metrics
	dev.LastStatus = t.Status
	dev.Status = "ONLINE"
	dev.TotalTelemetries++

	r.telemetries[deviceID] = append(r.telemetries[deviceID], *t)
	return nil
}

func (r *InMemoryRepository) SaveEvent(e *models.EventPayload) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	deviceID := e.Header.DeviceID
	r.events[deviceID] = append(r.events[deviceID], *e)
	return nil
}

func (r *InMemoryRepository) GetDevice(deviceID string) (*models.DeviceState, error) {
	r.mu.RLock()
	defer r.mu.RUnlock()

	dev, exists := r.devices[deviceID]
	if !exists {
		return nil, ErrDeviceNotFound
	}
	return dev, nil
}

func (r *InMemoryRepository) ListDevices() []*models.DeviceState {
	r.mu.RLock()
	defer r.mu.RUnlock()

	list := make([]*models.DeviceState, 0, len(r.devices))
	for _, d := range r.devices {
		list = append(list, d)
	}
	return list
}

func (r *InMemoryRepository) GetTelemetryHistory(deviceID string, limit int) ([]models.TelemetryPayload, error) {
	r.mu.RLock()
	defer r.mu.RUnlock()

	hist, exists := r.telemetries[deviceID]
	if !exists {
		return []models.TelemetryPayload{}, nil
	}
	if limit > 0 && len(hist) > limit {
		return hist[len(hist)-limit:], nil
	}
	return hist, nil
}

func (r *InMemoryRepository) QueueCommand(deviceID string, cmd *models.DeviceCommand) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.commands[deviceID] = append(r.commands[deviceID], cmd)
	return nil
}

func (r *InMemoryRepository) GetPendingCommands(deviceID string) ([]models.DeviceCommand, error) {
	r.mu.RLock()
	defer r.mu.RUnlock()
	cmds, exists := r.commands[deviceID]
	if !exists {
		return []models.DeviceCommand{}, nil
	}
	var res []models.DeviceCommand
	for _, c := range cmds {
		res = append(res, *c)
	}
	return res, nil
}

func (r *InMemoryRepository) AckCommand(deviceID, commandID, result string) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	cmds := r.commands[deviceID]
	var remaining []*models.DeviceCommand
	for _, c := range cmds {
		if c.CommandID != commandID {
			remaining = append(remaining, c)
		}
	}
	r.commands[deviceID] = remaining
	return nil
}



