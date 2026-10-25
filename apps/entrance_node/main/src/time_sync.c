#include "time_sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "config.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "sync.h"

static const char *TAG = "TIME_SYNC";

static bool s_time_valid = false;
static bool s_sntp_started = false;
static bool s_sntp_synced = false;

static void sntp_sync_cb(struct timeval *tv)
{
    (void)tv;
    s_time_valid = true;
    s_sntp_synced = true;
    sync_event_set(SYS_EVT_TIME_SYNCED);
    ESP_LOGI(TAG, "Wall clock synchronized via SNTP");
}

esp_err_t time_sync_init(void)
{
    setenv("TZ", CFG_TIMEZONE_POSIX, 1);
    tzset();

    struct timeval tv = {
        .tv_sec = (time_t)CFG_DEFAULT_BOOT_EPOCH,
        .tv_usec = 0
    };
    settimeofday(&tv, NULL);
    s_time_valid = true;
    sync_event_set(SYS_EVT_TIME_SYNCED);

    char iso[32];
    time_sync_format_iso(CFG_DEFAULT_BOOT_EPOCH, iso, sizeof(iso));
    ESP_LOGI(TAG, "Initialized EAT (UTC+3) clock to %s", iso);
    return ESP_OK;
}

esp_err_t time_sync_start_sntp(void)
{
    if (s_sntp_started) {
        return ESP_OK;
    }

    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CFG_SNTP_SERVER);
    sntp_cfg.sync_cb = sntp_sync_cb;
    esp_err_t err = esp_netif_sntp_init(&sntp_cfg);
    if (err == ESP_OK) {
        s_sntp_started = true;
        ESP_LOGI(TAG, "SNTP client started against %s", CFG_SNTP_SERVER);
    } else {
        ESP_LOGW(TAG, "SNTP init warning: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t time_sync_set_datetime(int year, int month, int day, int hour, int minute, int second)
{
    if (year < 2024 || year > 2099 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59) {
        return ESP_ERR_INVALID_ARG;
    }

    struct tm t = { 0 };
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    t.tm_isdst = -1;

    time_t epoch = mktime(&t);
    if (epoch < 0) {
        return ESP_FAIL;
    }

    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    s_time_valid = true;
    sync_event_set(SYS_EVT_TIME_SYNCED);

    char buf[32];
    time_sync_format_iso((int64_t)epoch, buf, sizeof(buf));
    ESP_LOGI(TAG, "System clock manually set to %s EAT", buf);
    return ESP_OK;
}

esp_err_t time_sync_get_now(time_snapshot_t *out_now)
{
    if (out_now == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    time_t now = time(NULL);
    struct tm tm_info = { 0 };
    localtime_r(&now, &tm_info);

    out_now->epoch = (int64_t)now;
    out_now->year = tm_info.tm_year + 1900;
    out_now->month = tm_info.tm_mon + 1;
    out_now->day = tm_info.tm_mday;
    out_now->hour = tm_info.tm_hour;
    out_now->minute = tm_info.tm_min;
    out_now->second = tm_info.tm_sec;
    out_now->day_of_week = tm_info.tm_wday;
    out_now->time_valid = s_time_valid && (tm_info.tm_year + 1900 >= 2024);
    out_now->sntp_synced = s_sntp_synced;
    return ESP_OK;
}

void time_sync_format_iso(int64_t epoch, char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }

    time_t t = (time_t)epoch;
    struct tm tm_info = { 0 };
    localtime_r(&t, &tm_info);
    snprintf(buf, buf_len, "%04d-%02d-%02d %02d:%02d:%02d",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
}

void time_sync_format_lcd(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }

    static const char *const s_days[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    time_snapshot_t now;
    time_sync_get_now(&now);
    const char *dname = (now.day_of_week >= 0 && now.day_of_week < 7) ? s_days[now.day_of_week] : "DAY";
    snprintf(buf, buf_len, "%s %02d:%02d:%02d EAT", dname, now.hour, now.minute, now.second);
}

bool time_sync_is_valid(void)
{
    time_snapshot_t now;
    time_sync_get_now(&now);
    return now.time_valid;
}
