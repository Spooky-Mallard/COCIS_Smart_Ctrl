#include "net_wifi.h"
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "sync.h"
#include "time_sync.h"

static const char *TAG = "NET_WIFI";

static bool          s_connected = false;
static char          s_ip_str[16] = "0.0.0.0";
static uint32_t      s_reconnects = 0;
static uint32_t      s_backoff_ms = CFG_WIFI_RECONNECT_BASE_MS;
static TimerHandle_t s_reconn_timer = NULL;

static void reconnect_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (!s_connected) {
        esp_wifi_connect();
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        snprintf(s_ip_str, sizeof(s_ip_str), "0.0.0.0");
        sync_event_clear(SYS_EVT_WIFI_CONNECTED);
        s_reconnects++;

        if (s_reconn_timer != NULL) {
            xTimerChangePeriod(s_reconn_timer, pdMS_TO_TICKS(s_backoff_ms), 0);
            xTimerStart(s_reconn_timer, 0);
        }
        if (s_backoff_ms < CFG_WIFI_RECONNECT_MAX_MS) {
            s_backoff_ms *= 2;
            if (s_backoff_ms > CFG_WIFI_RECONNECT_MAX_MS) {
                s_backoff_ms = CFG_WIFI_RECONNECT_MAX_MS;
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        s_backoff_ms = CFG_WIFI_RECONNECT_BASE_MS;
        sync_event_set(SYS_EVT_WIFI_CONNECTED);
        ESP_LOGI(TAG, "Connected to %s (IP: %s)", CFG_WIFI_SSID, s_ip_str);
        time_sync_start_sntp();
    }
}

esp_err_t net_wifi_init(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi init returned %s (continuing with local virtual bus)", esp_err_to_name(err));
        return err;
    }

    s_reconn_timer = xTimerCreate("wifi_reconn", pdMS_TO_TICKS(CFG_WIFI_RECONNECT_BASE_MS),
                                  pdFALSE, NULL, reconnect_timer_cb);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_cfg = { 0 };
    strncpy((char *)wifi_cfg.sta.ssid, CFG_WIFI_SSID, sizeof(wifi_cfg.sta.ssid) - 1);
    strncpy((char *)wifi_cfg.sta.password, CFG_WIFI_PASSWORD, sizeof(wifi_cfg.sta.password) - 1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_start();

    ESP_LOGI(TAG, "Wi-Fi STA started (Target SSID: '%s')", CFG_WIFI_SSID);
    return ESP_OK;
}

bool net_wifi_is_connected(void)
{
    return s_connected;
}

esp_err_t net_wifi_get_status(wifi_status_t *out_status)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out_status->connected = s_connected;
    strncpy(out_status->ssid, CFG_WIFI_SSID, sizeof(out_status->ssid) - 1);
    out_status->ssid[sizeof(out_status->ssid) - 1] = '\0';
    strncpy(out_status->ip_addr, s_ip_str, sizeof(out_status->ip_addr) - 1);
    out_status->ip_addr[sizeof(out_status->ip_addr) - 1] = '\0';
    out_status->reconnect_count = s_reconnects;
    out_status->rssi = 0;

    if (s_connected) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            out_status->rssi = ap_info.rssi;
        }
    }
    return ESP_OK;
}
