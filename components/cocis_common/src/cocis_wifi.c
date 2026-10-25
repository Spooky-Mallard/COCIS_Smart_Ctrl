#include "cocis_wifi.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cocis_config.h"
#include "cocis_sync.h"

static const char *TAG = "cocis_wifi";

static bool s_connected = false;
static int s_retry_count = 0;
static char s_ip_str[16] = "0.0.0.0";
static esp_netif_t *s_netif = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi station started, connecting to %s...", CFG_WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        cocis_sync_event_clear(SYS_EVT_WIFI_CONNECTED);
        s_retry_count++;
        uint32_t delay_ms = (s_retry_count > 5) ? 5000 : (500 * s_retry_count);
        ESP_LOGW(TAG, "Disconnected from AP, reconnecting in %lu ms...", (unsigned long)delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_ip_str, sizeof(s_ip_str));
        s_connected = true;
        s_retry_count = 0;
        cocis_sync_event_set(SYS_EVT_WIFI_CONNECTED);
        ESP_LOGI(TAG, "Station connected! IP: %s (SSID: %s)", s_ip_str, CFG_WIFI_SSID);
    }
}

esp_err_t cocis_wifi_init(void)
{
    /* Initialize NVS if not already done */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default();

    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CFG_WIFI_SSID,
            .password = CFG_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Wi-Fi station subsystem initialized");
    return ESP_OK;
}

bool cocis_wifi_is_connected(void)
{
    return s_connected;
}

int8_t cocis_wifi_get_rssi(void)
{
    if (!s_connected) {
        return -127;
    }
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return -55; /* Default simulated RSSI on Wokwi */
}

void cocis_wifi_get_ip_str(char *buf, size_t len)
{
    if (buf && len > 0) {
        strncpy(buf, s_ip_str, len - 1);
        buf[len - 1] = '\0';
    }
}
