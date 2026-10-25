#ifndef NET_WIFI_H
#define NET_WIFI_H

#include "cocis_wifi.h"

typedef struct {
    bool   connected;
    char   ip_addr[16];
    int8_t rssi;
} wifi_status_t;

static inline esp_err_t net_wifi_init(void) {
    return cocis_wifi_init();
}

static inline bool net_wifi_is_connected(void) {
    return cocis_wifi_is_connected();
}

static inline int8_t net_wifi_get_rssi(void) {
    return cocis_wifi_get_rssi();
}

static inline void net_wifi_get_ip_str(char *buf, size_t len) {
    cocis_wifi_get_ip_str(buf, len);
}

static inline esp_err_t net_wifi_get_status(wifi_status_t *out_st) {
    if (out_st) {
        out_st->connected = cocis_wifi_is_connected();
        out_st->rssi = cocis_wifi_get_rssi();
        cocis_wifi_get_ip_str(out_st->ip_addr, sizeof(out_st->ip_addr));
    }
    return ESP_OK;
}

#endif /* NET_WIFI_H */
