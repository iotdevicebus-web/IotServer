#!/usr/bin/env bash
set -euo pipefail

echo "=========================================================="
echo "      IoT Platform Project - Full Local Test Suite        "
echo "=========================================================="

echo "[1/4] Running mTLS Certificate Generation..."
cd tools/certs
./generate_certs.sh "DEV-ESP32-001" 365 >/dev/null
./generate_certs.sh "DEV-STM32-002" 365 >/dev/null
cd ../..
echo "  -> Certs OK!"

echo "[2/4] Running Go Server & Storage Unit Tests..."
cd server
go test ./tests >/dev/null
cd ..
echo "  -> Server Tests OK!"

echo "[3/4] Running Full E2E & OTA & DB & Web Integration Tests..."
cd tools/test_runner
go run run_e2e.go
cd ../..

echo "=========================================================="
echo "  >>> ALL LOCAL CI/CD TEST PIPELINES PASSED! <<<          "
echo "=========================================================="
