#ifndef COCIS_WIFI_H
#define COCIS_WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cocis_wifi_init(void);

bool cocis_wifi_is_connected(void);

int8_t cocis_wifi_get_rssi(void);

void cocis_wifi_get_ip_str(char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* COCIS_WIFI_H */
