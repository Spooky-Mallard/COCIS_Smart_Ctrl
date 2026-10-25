#include "cocis_crypto.h"
#include <string.h>
#include <stdio.h>
#include "psa/crypto.h"
#include "esp_log.h"

static const char *TAG = "cocis_crypto";
static bool s_crypto_ready = false;

esp_err_t cocis_crypto_init(void)
{
    if (s_crypto_ready) {
        return ESP_OK;
    }
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_crypto_init failed: %ld", (long)status);
        return ESP_FAIL;
    }
    s_crypto_ready = true;
    ESP_LOGI(TAG, "PSA Crypto subsystem initialized for HMAC-SHA256 authentication");
    return ESP_OK;
}

esp_err_t cocis_crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                                   const uint8_t *data, size_t data_len,
                                   uint8_t out_mac[32])
{
    if (!s_crypto_ready) {
        esp_err_t err = cocis_crypto_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    uint8_t k_block[64];
    memset(k_block, 0, sizeof(k_block));

    if (key_len > 64) {
        size_t hlen = 0;
        psa_status_t st = psa_hash_compute(PSA_ALG_SHA_256, key, key_len, k_block, sizeof(k_block), &hlen);
        if (st != PSA_SUCCESS) {
            return ESP_FAIL;
        }
    } else {
        memcpy(k_block, key, key_len);
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (size_t i = 0; i < 64; i++) {
        ipad[i] = k_block[i] ^ 0x36;
        opad[i] = k_block[i] ^ 0x5C;
    }

    /* Inner hash: H(ipad || data) */
    uint8_t inner_hash[32];
    size_t actual_len = 0;
    psa_hash_operation_t op = psa_hash_operation_init();
    if (psa_hash_setup(&op, PSA_ALG_SHA_256) != PSA_SUCCESS) return ESP_FAIL;
    if (psa_hash_update(&op, ipad, sizeof(ipad)) != PSA_SUCCESS) { psa_hash_abort(&op); return ESP_FAIL; }
    if (psa_hash_update(&op, data, data_len) != PSA_SUCCESS) { psa_hash_abort(&op); return ESP_FAIL; }
    if (psa_hash_finish(&op, inner_hash, sizeof(inner_hash), &actual_len) != PSA_SUCCESS) return ESP_FAIL;

    /* Outer hash: H(opad || inner_hash) */
    op = psa_hash_operation_init();
    if (psa_hash_setup(&op, PSA_ALG_SHA_256) != PSA_SUCCESS) return ESP_FAIL;
    if (psa_hash_update(&op, opad, sizeof(opad)) != PSA_SUCCESS) { psa_hash_abort(&op); return ESP_FAIL; }
    if (psa_hash_update(&op, inner_hash, sizeof(inner_hash)) != PSA_SUCCESS) { psa_hash_abort(&op); return ESP_FAIL; }
    if (psa_hash_finish(&op, out_mac, 32, &actual_len) != PSA_SUCCESS) return ESP_FAIL;

    return ESP_OK;
}

esp_err_t cocis_crypto_hmac_hex(const char *key, const char *payload, char out_hex[65])
{
    if (!key || !payload || !out_hex) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mac[32];
    esp_err_t err = cocis_crypto_hmac_sha256((const uint8_t *)key, strlen(key),
                                            (const uint8_t *)payload, strlen(payload),
                                            mac);
    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < 32; i++) {
        sprintf(&out_hex[i * 2], "%02x", mac[i]);
    }
    out_hex[64] = '\0';
    return ESP_OK;
}

bool cocis_crypto_verify_hmac(const char *key, const char *payload, const char *expected_hex)
{
    if (!key || !payload || !expected_hex) {
        return false;
    }

    char computed_hex[65];
    if (cocis_crypto_hmac_hex(key, payload, computed_hex) != ESP_OK) {
        return false;
    }

    /* Constant-time comparison to protect against timing attacks */
    int diff = 0;
    for (int i = 0; i < 64; i++) {
        diff |= (computed_hex[i] ^ expected_hex[i]);
    }
    return (diff == 0);
}
