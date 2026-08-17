#!/usr/bin/env pwsh
<#
.SYNOPSIS
    IoT Platform mTLS 証明書一括生成スクリプト (PowerShell版)
.DESCRIPTION
    1. Root CA (プライベート認証局) の生成
    2. Server 証明書の発行 (mTLS サーバ用)
    3. Client 証明書の発行 (エッジデバイス用)
    4. C言語ヘッダファイル (C header) の自動生成
.PARAMETER DeviceId
    発行するエッジデバイスのID (デフォルト: DEV-ESP32-001)
.PARAMETER Days
    有効期限日数 (デフォルト: 365)
#>

param(
    [string]$DeviceId = "DEV-ESP32-001",
    [int]$Days = 365
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$outputDir = Join-Path $scriptDir "out"

if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "  IoT Platform mTLS 証明書生成ツール" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# 1. Root CA
$caKey = Join-Path $outputDir "ca.key"
$caCrt = Join-Path $outputDir "ca.crt"

if (-not (Test-Path $caCrt)) {
    Write-Host "[1/3] Root CA (プライベート認証局) を生成中..." -ForegroundColor Green
    openssl req -x509 -new -nodes -config "$scriptDir/ca.cnf" -keyout $caKey -out $caCrt -days ($Days * 5)
    Write-Host "  -> Root CA 作成完了: $caCrt" -ForegroundColor Gray
} else {
    Write-Host "[1/3] 既存の Root CA を使用します: $caCrt" -ForegroundColor Yellow
}

# 2. Server Certificate
$serverKey = Join-Path $outputDir "server.key"
$serverCsr = Join-Path $outputDir "server.csr"
$serverCrt = Join-Path $outputDir "server.crt"

Write-Host "[2/3] サーバ証明書を発行中 (localhost, 127.0.0.1)..." -ForegroundColor Green
openssl req -new -nodes -config "$scriptDir/server.cnf" -keyout $serverKey -out $serverCsr
openssl x509 -req -in $serverCsr -CA $caCrt -CAkey $caKey -CAcreateserial -out $serverCrt -days $Days -extfile "$scriptDir/server.cnf" -extensions v3_req
Remove-Item -Force $serverCsr
Write-Host "  -> サーバ証明書発行完了: $serverCrt" -ForegroundColor Gray

# 3. Client Certificate for Device
$clientKey = Join-Path $outputDir "client_$DeviceId.key"
$clientCsr = Join-Path $outputDir "client_$DeviceId.csr"
$clientCrt = Join-Path $outputDir "client_$DeviceId.crt"

Write-Host "[3/3] クライアント証明書を発行中 (DeviceId: $DeviceId)..." -ForegroundColor Green

# デバイス用カスタムcnfの作成
$deviceCnf = Join-Path $outputDir "temp_$DeviceId.cnf"
$cnfTemplate = Get-Content "$scriptDir/client.cnf" -Raw
$cnfContent = $cnfTemplate -replace "DEV-ESP32-001", $DeviceId
Set-Content -Path $deviceCnf -Value $cnfContent

openssl req -new -nodes -config $deviceCnf -keyout $clientKey -out $clientCsr
openssl x509 -req -in $clientCsr -CA $caCrt -CAkey $caKey -CAcreateserial -out $clientCrt -days $Days -extfile $deviceCnf -extensions v3_req
Remove-Item -Force $clientCsr
Remove-Item -Force $deviceCnf
Write-Host "  -> クライアント証明書発行完了: $clientCrt" -ForegroundColor Gray

# 4. エッジファームウェア向け C言語ヘッダファイルの生成
$headerFile = Join-Path $outputDir "device_certs.h"
Write-Host "エッジ用 C言語ヘッダ ($headerFile) を出力中..." -ForegroundColor Green

# クライアント秘密鍵を MbedTLS (ESP32) 互換の PKCS#1 (BEGIN RSA PRIVATE KEY) 形式に変換
$pkcs1Key = Join-Path $outputDir "client_${DeviceId}_rsa.key"
openssl rsa -in $clientKey -traditional -out $pkcs1Key 2>$null
if (Test-Path $pkcs1Key) {
    $clientKeyContent = (Get-Content $pkcs1Key -Raw).Trim()
} else {
    $clientKeyContent = (Get-Content $clientKey -Raw).Trim()
}


$caCrtContent = (Get-Content $caCrt -Raw).Trim()
$clientCrtContent = (Get-Content $clientCrt -Raw).Trim()



$cHeader = @"
/**
 * @file device_certs.h
 * @brief Auto-generated mTLS certificates for $DeviceId
 * @note Generated at $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
 */
#ifndef DEVICE_CERTS_H
#define DEVICE_CERTS_H

#define IOT_DEVICE_ID "$DeviceId"

static const char IOT_ROOT_CA_CERT[] = 
$(($caCrtContent -split "`r?`n" | ForEach-Object { "    `"$_`\n`"" }) -join "`n");

static const char IOT_DEVICE_CLIENT_CERT[] = 
$(($clientCrtContent -split "`r?`n" | ForEach-Object { "    `"$_`\n`"" }) -join "`n");

static const char IOT_DEVICE_PRIVATE_KEY[] = 
$(($clientKeyContent -split "`r?`n" | ForEach-Object { "    `"$_`\n`"" }) -join "`n");

#endif // DEVICE_CERTS_H
"@

Set-Content -Path $headerFile -Value $cHeader

Write-Host "  -> Cヘッダ生成完了!" -ForegroundColor Gray

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host "証明書生成が正常に完了しました。" -ForegroundColor Cyan
Write-Host "出力先: $outputDir" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan
