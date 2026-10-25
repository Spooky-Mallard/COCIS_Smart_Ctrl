#ifndef COCIS_CRYPTO_H
#define COCIS_CRYPTO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cocis_crypto_init(void);

esp_err_t cocis_crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                                   const uint8_t *data, size_t data_len,
                                   uint8_t out_mac[32]);

esp_err_t cocis_crypto_hmac_hex(const char *key, const char *payload, char out_hex[65]);

bool cocis_crypto_verify_hmac(const char *key, const char *payload, const char *expected_hex);

#ifdef __cplusplus
}
#endif

#endif /* COCIS_CRYPTO_H */
