# ==========================================
# Multi-stage Dockerfile for IoT Management Server
# ==========================================

# 1. Build Stage
FROM golang:1.25-alpine AS builder

WORKDIR /app

# CA証明書とビルドツールのインストール
RUN apk add --no-cache git ca-certificates tzdata

# 依存モジュールのキャッシュ
COPY go.mod go.sum* ./
RUN go mod download

# ソースコードのコピー
COPY . .

# 静的シングルバイナリのビルド
RUN CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -ldflags="-w -s" -o /app/bin/iot-server ./main.go

# ==========================================
# 2. Runtime Stage (Lightweight & Secure)
# ==========================================
FROM alpine:3.20

WORKDIR /app

# タイムゾーンと最小限のユーティリティ
RUN apk --no-cache add ca-certificates tzdata \
    && addgroup -S iotgroup && adduser -S iotuser -G iotgroup \
    && mkdir -p /app/data /app/certs /app/web \
    && chown -R iotuser:iotgroup /app

# ビルド済みバイナリとWebアセットのコピー
COPY --from=builder /app/bin/iot-server /app/iot-server
COPY ./web/index.html /app/web/index.html

# 実行ユーザーの切り替え
USER iotuser

# エクスポーズポート (8443: mTLS, 8080: Web Dashboard)
EXPOSE 8443 8080

# デフォルト環境変数
ENV PORT=8443 \
    REQUIRE_MTLS=true \
    SERVER_CERT=/app/certs/server.crt \
    SERVER_KEY=/app/certs/server.key \
    ROOT_CA_CERT=/app/certs/ca.crt \
    DB_DRIVER=sqlite \
    DB_DSN=/app/data/iot_platform.db

# ヘルスチェック
HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
    CMD wget --no-check-certificate -qO- http://localhost:8080/healthz || exit 1

ENTRYPOINT ["/app/iot-server"]
