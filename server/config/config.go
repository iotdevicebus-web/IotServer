package config

import (
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"os"
)

type Config struct {
	Port         string
	RequireMTLS  bool
	ServerCert   string
	ServerKey    string
	RootCACert   string
	DBDriver     string
	DBDSN        string
	WebhookURL   string
}

func LoadConfig() *Config {
	return &Config{
		Port:        getEnv("PORT", "8443"),
		RequireMTLS: getEnv("REQUIRE_MTLS", "true") == "true",
		ServerCert:  getEnv("SERVER_CERT", "../tools/certs/out/server.crt"),
		ServerKey:   getEnv("SERVER_KEY", "../tools/certs/out/server.key"),
		RootCACert:  getEnv("ROOT_CA_CERT", "../tools/certs/out/ca.crt"),
		DBDriver:    getEnv("DB_DRIVER", "sqlite"),
		DBDSN:       getEnv("DB_DSN", "iot_platform.db"),
		WebhookURL:  getEnv("WEBHOOK_URL", ""),
	}
}



func (c *Config) BuildTLSConfig() (*tls.Config, error) {
	// サーバ証明書・秘密鍵のロード
	cert, err := tls.LoadX509KeyPair(c.ServerCert, c.ServerKey)
	if err != nil {
		return nil, fmt.Errorf("failed to load server keypair: %w", err)
	}

	// クライアント検証用 CA 証明書のロード
	caCertPEM, err := os.ReadFile(c.RootCACert)
	if err != nil {
		return nil, fmt.Errorf("failed to read root CA cert: %w", err)
	}

	caPool := x509.NewCertPool()
	if !caPool.AppendCertsFromPEM(caCertPEM) {
		return nil, fmt.Errorf("failed to parse root CA cert into pool")
	}

	clientAuth := tls.RequireAndVerifyClientCert
	if !c.RequireMTLS {
		clientAuth = tls.NoClientCert
	}

	tlsConfig := &tls.Config{
		Certificates: []tls.Certificate{cert},
		ClientCAs:    caPool,
		ClientAuth:   clientAuth,
		MinVersion:   tls.VersionTLS12,
	}

	return tlsConfig, nil
}

func getEnv(key, defaultVal string) string {
	if val := os.Getenv(key); val != "" {
		return val
	}
	return defaultVal
}
