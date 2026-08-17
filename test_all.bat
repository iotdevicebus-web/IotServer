@echo off
setlocal enabledelayedexpansion

echo ==========================================================
echo       IoT Platform Project - Full Local Test Suite
echo ==========================================================

echo [1/4] Running mTLS Certificate Generation...
cd tools\certs
call powershell -NoProfile -ExecutionPolicy Bypass -File .\generate_certs.ps1 -DeviceId "DEV-ESP32-001" >nul 2>&1
call powershell -NoProfile -ExecutionPolicy Bypass -File .\generate_certs.ps1 -DeviceId "DEV-STM32-002" >nul 2>&1
cd ..\..
echo   - Certs OK!

echo [2/4] Running Edge Firmware Unit and Mock Tests...
call edge\tests\run_tests.bat >nul 2>&1
if %errorlevel% neq 0 (
    echo [FAIL] Edge unit tests failed!
    exit /b 1
)
echo   - Edge Tests OK!

echo [3/4] Running Go Server and Storage Unit Tests...
cd server
go test ./tests >nul 2>&1
if %errorlevel% neq 0 (
    echo [FAIL] Go Server tests failed!
    cd ..
    exit /b 1
)
cd ..
echo   - Server Tests OK!

echo [4/4] Running Full E2E and OTA and DB and Web Tests...
cd tools\test_runner
go run run_e2e.go
set E2E_RESULT=%errorlevel%
cd ..\..

if %E2E_RESULT% equ 0 (
    echo.
    echo ==========================================================
    echo   ALL LOCAL CI/CD TEST PIPELINES PASSED SUCCESSFULLY!
    echo ==========================================================
) else (
    echo.
    echo [FAIL] E2E Tests failed with code %E2E_RESULT%!
)

exit /b %E2E_RESULT%
