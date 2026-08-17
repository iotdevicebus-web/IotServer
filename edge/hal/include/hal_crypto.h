/**
 * @file hal_crypto.h
 * @brief 暗号ハードウェア・セキュアエレメント (ATECC608 / OPTIGA Trust) 抽象化インターフェース
 */

#ifndef HAL_CRYPTO_H
#define HAL_CRYPTO_H

#include "hal_types.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ハードウェア乱数生成器 (TRNG) から乱数バイト列を取得
 */
hal_status_t hal_crypto_get_random(uint8_t *buffer, size_t len);

/**
 * @brief SHA-256 ハッシュ計算
 */
hal_status_t hal_crypto_sha256(const uint8_t *input, size_t input_len, uint8_t *output_hash_32b);

/* ========================================================
 * セキュアエレメント (Secure Element / HSM) インターフェース
 * (Microchip ATECC608 / Infineon OPTIGA / ESP32 Vault 対応)
 * ======================================================== */

/**
 * @brief セキュアエレメント (I2C/SPI) の初期化 & 接続確認
 */
hal_status_t hal_crypto_se_init(void);

/**
 * @brief セキュアエレメント内のスロットに保管された秘密鍵を用いて ECDSA (P-256) 署名を生成
 * @note 秘密鍵はチップ外に一切読み出されず、チップ内部で署名演算が完結する
 * @param slot_id 秘密鍵スロット番号 (例: 0)
 * @param digest_32b 署名対象の SHA-256 ダイジェスト (32バイト)
 * @param out_sig_64b 出力先署名バッファ (R 32バイト + S 32バイト = 64バイト)
 */
hal_status_t hal_crypto_se_sign_digest(
    uint8_t slot_id,
    const uint8_t *digest_32b,
    uint8_t *out_sig_64b
);

/**
 * @brief セキュアエレメント内の指定スロットに対応する公開鍵を取得
 * @param slot_id スロット番号
 * @param out_pubkey_64b 出力先公開鍵 (X 32バイト + Y 32バイト = 64バイト)
 */
hal_status_t hal_crypto_se_get_public_key(
    uint8_t slot_id,
    uint8_t *out_pubkey_64b
);

/**
 * @brief セキュアエレメントがオンラインで正常か検証
 */
bool hal_crypto_se_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif // HAL_CRYPTO_H
