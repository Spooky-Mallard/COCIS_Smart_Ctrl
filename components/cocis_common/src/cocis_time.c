#include "cocis_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "cocis_config.h"
#include "cocis_sync.h"

static const char *TAG = "cocis_time";
static bool s_time_synced = false;

static void sntp_sync_cb(struct timeval *tv)
{
    (void)tv;
    s_time_synced = true;
    cocis_sync_event_set(SYS_EVT_TIME_SYNCED);
    char buf[32];
    cocis_time_format_now(buf, sizeof(buf));
    ESP_LOGI(TAG, "SNTP synchronized network time: %s EAT", buf);
}

esp_err_t cocis_time_init(void)
{
    setenv("TZ", CFG_TIMEZONE, 1);
    tzset();

    /* Deterministic initial seed: 2026-10-25 08:00:00 EAT (1792904400) */
    struct timeval tv_seed = {
        .tv_sec = 1792904400,
        .tv_usec = 0
    };
    settimeofday(&tv_seed, NULL);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CFG_SNTP_SERVER);
    config.sync_cb = sntp_sync_cb;
    esp_netif_sntp_init(&config);

    char buf[32];
    cocis_time_format_now(buf, sizeof(buf));
    ESP_LOGI(TAG, "Time subsystem initialized (Current seed: %s)", buf);
    return ESP_OK;
}

int64_t cocis_time_now_epoch(void)
{
    return (int64_t)time(NULL);
}

void cocis_time_format_now(char *buf, size_t len)
{
    cocis_time_format_epoch(cocis_time_now_epoch(), buf, len);
}

void cocis_time_format_epoch(int64_t epoch, char *buf, size_t len)
{
    time_t t = (time_t)epoch;
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm_info);
}

bool cocis_time_is_synced(void)
{
    return s_time_synced;
}

void cocis_time_set_epoch(int64_t epoch)
{
    struct timeval tv = {
        .tv_sec = (time_t)epoch,
        .tv_usec = 0
    };
    settimeofday(&tv, NULL);
    s_time_synced = true;
    cocis_sync_event_set(SYS_EVT_TIME_SYNCED);
}
