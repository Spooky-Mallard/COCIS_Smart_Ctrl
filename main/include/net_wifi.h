#ifndef NET_WIFI_H
#define NET_WIFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool     connected;
    char     ssid[33];
    char     ip_addr[16];
    int8_t   rssi;
    uint32_t reconnect_count;
} wifi_status_t;

esp_err_t net_wifi_init(void);
bool      net_wifi_is_connected(void);
esp_err_t net_wifi_get_status(wifi_status_t *out_status);

#endif
