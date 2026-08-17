package main

import (
	"context"
	"iot-platform-server/api/handlers"
	"iot-platform-server/api/middleware"
	"iot-platform-server/config"
	"iot-platform-server/storage"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"
)

func main() {
	log.Println("====================================================")
	log.Println("     Starting IoT Platform Management Server        ")
	log.Println("====================================================")

	cfg := config.LoadConfig()
	
	var repo storage.Repository
	if cfg.DBDriver == "memory" {
		repo = storage.NewInMemoryRepository()
	} else {
		sqlRepo, err := storage.NewSQLRepository(cfg.DBDriver, cfg.DBDSN)
		if err != nil {
			log.Fatalf("[FATAL] Database connection failed: %v", err)
		}
		repo = sqlRepo
	}

	apiHandler := handlers.NewApiHandler(repo, cfg.WebhookURL)

	// ルーティング設定 (エッジ用 mTLS)

	edgeMux := http.NewServeMux()
	edgeMux.HandleFunc("/healthz", apiHandler.HandleHealth)

	mtlsMiddleware := middleware.MTLSAuthMiddleware(cfg.RequireMTLS)
	edgeMux.Handle("/api/v1/telemetry", mtlsMiddleware(http.HandlerFunc(apiHandler.HandleTelemetry)))
	edgeMux.Handle("/api/v1/events", mtlsMiddleware(http.HandlerFunc(apiHandler.HandleEvents)))
	edgeMux.Handle("/api/v1/devices", mtlsMiddleware(http.HandlerFunc(apiHandler.HandleListDevices)))
	edgeMux.Handle("/api/v1/ota/download/", mtlsMiddleware(http.HandlerFunc(apiHandler.HandleOtaDownload)))
	edgeMux.Handle("/api/v1/telemetry/history", mtlsMiddleware(http.HandlerFunc(apiHandler.HandleTelemetryHistory)))
	edgeMux.Handle("/api/v1/commands", mtlsMiddleware(http.HandlerFunc(apiHandler.HandleQueueCommand)))
	edgeMux.Handle("/api/v1/commands/ack", mtlsMiddleware(http.HandlerFunc(apiHandler.HandleAckCommand)))

	// ルーティング設定 (管理者用 Web ダッシュボード HTTP :8080)
	webMux := http.NewServeMux()
	webMux.HandleFunc("/healthz", apiHandler.HandleHealth)
	webMux.HandleFunc("/api/v1/devices", apiHandler.HandleListDevices)
	webMux.HandleFunc("/api/v1/telemetry/history", apiHandler.HandleTelemetryHistory)
	webMux.HandleFunc("/api/v1/commands", apiHandler.HandleQueueCommand)
	webMux.HandleFunc("/api/v1/commands/ack", apiHandler.HandleAckCommand)

	// 静的ファイル配信 (server/web/)
	fs := http.FileServer(http.Dir("./web"))
	webMux.Handle("/", fs)

	// システム総合仕様書 & 取扱説明書 & 画像ファイル配信
	webMux.HandleFunc("/spec", func(w http.ResponseWriter, r *http.Request) {
		http.ServeFile(w, r, "../docs/system_specification.html")
	})
	webMux.HandleFunc("/guide", func(w http.ResponseWriter, r *http.Request) {
		http.ServeFile(w, r, "../docs/server_user_guide.html")
	})
	webMux.Handle("/images/", http.StripPrefix("/images/", http.FileServer(http.Dir("../docs/images"))))





	tlsConfig, err := cfg.BuildTLSConfig()
	if err != nil {
		log.Fatalf("[FATAL] TLS Configuration error: %v", err)
	}

	edgeServer := &http.Server{
		Addr:         ":" + cfg.Port,
		Handler:      edgeMux,
		TLSConfig:    tlsConfig,
		ReadTimeout:  10 * time.Second,
		WriteTimeout: 10 * time.Second,
		IdleTimeout:  60 * time.Second,
	}

	dashboardPort := "8080"
	dashboardServer := &http.Server{
		Addr:         ":" + dashboardPort,
		Handler:      webMux,
		ReadTimeout:  10 * time.Second,
		WriteTimeout: 10 * time.Second,
	}

	stopChan := make(chan os.Signal, 1)
	signal.Notify(stopChan, os.Interrupt, syscall.SIGTERM)

	// 1. エッジ用 mTLS サーバ起動 (:8443)
	go func() {
		log.Printf("[EDGE SERVER] Listening on HTTPS port %s (mTLS Required: %v)", cfg.Port, cfg.RequireMTLS)
		if err := edgeServer.ListenAndServeTLS("", ""); err != nil && err != http.ErrServerClosed {
			log.Fatalf("[FATAL] Edge Server failed: %v", err)
		}
	}()

	// 2. 管理用 Web ダッシュボード起動 (:8080)
	go func() {
		log.Printf("[DASHBOARD] Web UI Dashboard running at: http://localhost:%s/", dashboardPort)
		if err := dashboardServer.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Printf("[WARN] Dashboard Server stopped: %v", err)
		}
	}()

	<-stopChan
	log.Println("[SERVER] Shutting down gracefully...")

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	_ = edgeServer.Shutdown(ctx)
	_ = dashboardServer.Shutdown(ctx)
	log.Println("[SERVER] All servers exited cleanly.")

}
