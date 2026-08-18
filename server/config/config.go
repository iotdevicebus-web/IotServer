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
	serverCert := getEnv("SERVER_CERT", findCertPath("../tools/certs/out/server.crt", "certs/server.crt", "tools/certs/out/server.crt"))
	serverKey := getEnv("SERVER_KEY", findCertPath("../tools/certs/out/server.key", "certs/server.key", "tools/certs/out/server.key"))
	rootCACert := getEnv("ROOT_CA_CERT", findCertPath("../tools/certs/out/ca.crt", "certs/ca.crt", "tools/certs/out/ca.crt"))

	return &Config{
		Port:        getEnv("PORT", "8443"),
		RequireMTLS: getEnv("REQUIRE_MTLS", "true") == "true",
		ServerCert:  serverCert,
		ServerKey:   serverKey,
		RootCACert:  rootCACert,
		DBDriver:    getEnv("DB_DRIVER", "sqlite"),
		DBDSN:       getEnv("DB_DSN", "iot_platform.db"),
		WebhookURL:  getEnv("WEBHOOK_URL", ""),
	}
}

func findCertPath(paths ...string) string {
	for _, p := range paths {
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}
	if len(paths) > 0 {
		return paths[0]
	}
	return ""
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
