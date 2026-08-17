package middleware

import (
	"context"
	"crypto/x509"
	"encoding/json"
	"iot-platform-server/storage/models"
	"log"
	"net/http"
)

type contextKey string

const (
	DeviceIDKey contextKey = "authenticated_device_id"
	CertKey     contextKey = "client_certificate"
)

// MTLSAuthMiddleware クライアント証明書を検証し、Device IDをコンテキストに注入するミドルウェア
func MTLSAuthMiddleware(requireMTLS bool) func(http.Handler) http.Handler {
	return func(next http.Handler) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			if r.TLS == nil || len(r.TLS.PeerCertificates) == 0 {
				if requireMTLS {
					log.Printf("[SECURITY] Unauthorized: No client certificate presented from %s", r.RemoteAddr)
					w.Header().Set("Content-Type", "application/json")
					w.WriteHeader(http.StatusUnauthorized)
					json.NewEncoder(w).Encode(models.ApiResponse{
						Status:  "UNAUTHORIZED",
						Message: "mTLS client certificate required",
					})
					return
				}
				// 開発・テスト時のバイパスモード
				ctx := context.WithValue(r.Context(), DeviceIDKey, "DEV-ESP32-001")
				next.ServeHTTP(w, r.WithContext(ctx))
				return
			}

			clientCert := r.TLS.PeerCertificates[0]
			deviceID := clientCert.Subject.CommonName

			// ログ記録
			log.Printf("[SECURITY] mTLS Authenticated: DeviceID=%s (Issuer=%s, SAN=%v)",
				deviceID, clientCert.Issuer.CommonName, clientCert.DNSNames)

			// コンテキストに認証情報を格納
			ctx := context.WithValue(r.Context(), DeviceIDKey, deviceID)
			ctx = context.WithValue(ctx, CertKey, clientCert)

			next.ServeHTTP(w, r.WithContext(ctx))
		})
	}
}

// GetAuthenticatedDeviceID コンテキストから認証済みDevice IDを取得
func GetAuthenticatedDeviceID(ctx context.Context) (string, bool) {
	devID, ok := ctx.Value(DeviceIDKey).(string)
	return devID, ok
}

// GetClientCert コンテキストからクライアント証明書を取得
func GetClientCert(ctx context.Context) (*x509.Certificate, bool) {
	cert, ok := ctx.Value(CertKey).(*x509.Certificate)
	return cert, ok
}
