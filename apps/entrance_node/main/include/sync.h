#ifndef SYNC_H
#define SYNC_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

typedef enum {
    LOCK_LEVEL_NONE = 0,
    LOCK_LEVEL_CONFIG = 1,
    LOCK_LEVEL_USER_DB = 2,
    LOCK_LEVEL_SCHEDULE = 3,
    LOCK_LEVEL_LEDGER = 4,
    LOCK_LEVEL_ROOM = 5,
    LOCK_LEVEL_DISPLAY = 6,
    LOCK_LEVEL_SD = 7,
    LOCK_LEVEL_COUNT = 8
} sync_lock_level_t;

#define SYS_EVT_WIFI_CONNECTED   (1 << 0)
#define SYS_EVT_TIME_SYNCED      (1 << 1)
#define SYS_EVT_SD_MOUNTED       (1 << 2)
#define SYS_EVT_EMERGENCY_MODE   (1 << 3)
#define SYS_EVT_ADMIN_AUTH       (1 << 4)
#define SYS_EVT_MQTT_CONNECTED   (1 << 5)

typedef struct {
    const char *name;
    uint32_t acquire_count;
    uint32_t timeout_count;
} sync_lock_stats_t;

esp_err_t sync_init(void);
esp_err_t sync_lock(sync_lock_level_t level, uint32_t timeout_ms);
esp_err_t sync_unlock(sync_lock_level_t level);

void sync_event_set(EventBits_t bits);
void sync_event_clear(EventBits_t bits);
bool sync_event_is_set(EventBits_t bit);
EventBits_t sync_event_get_all(void);

esp_err_t sync_get_stats(sync_lock_level_t level, sync_lock_stats_t *out_stats);

#endif
