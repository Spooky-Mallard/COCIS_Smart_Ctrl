#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"

typedef struct {
    int64_t epoch;
    int     year;
    int     month;
    int     day;
    int     hour;
    int     minute;
    int     second;
    int     day_of_week; /* 0 = Sunday .. 6 = Saturday */
    bool    time_valid;
    bool    sntp_synced;
} time_snapshot_t;

esp_err_t time_sync_init(void);
esp_err_t time_sync_start_sntp(void);
esp_err_t time_sync_set_datetime(int year, int month, int day, int hour, int minute, int second);
esp_err_t time_sync_get_now(time_snapshot_t *out_now);
void      time_sync_format_iso(int64_t epoch, char *buf, size_t buf_len);
void      time_sync_format_lcd(char *buf, size_t buf_len);
bool      time_sync_is_valid(void);

#endif
