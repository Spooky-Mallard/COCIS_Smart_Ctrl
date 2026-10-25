#ifndef COCIS_SYNC_H
#define COCIS_SYNC_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COCIS_LOCK_LEVEL_SYSTEM  = 0,
    COCIS_LOCK_LEVEL_NETWORK = 1,
    COCIS_LOCK_LEVEL_DATA    = 2,
    COCIS_LOCK_LEVEL_COUNT   = 3
} cocis_lock_level_t;

#define COCIS_EVT_WIFI_CONNECTED    (1 << 0)
#define COCIS_EVT_MQTT_CONNECTED    (1 << 1)
#define COCIS_EVT_TIME_SYNCED       (1 << 2)
#define COCIS_EVT_EMERGENCY_ACTIVE  (1 << 3)

esp_err_t cocis_sync_init(void);

esp_err_t cocis_sync_lock(cocis_lock_level_t level, uint32_t timeout_ms);
void cocis_sync_unlock(cocis_lock_level_t level);

void cocis_sync_event_set(uint32_t bits);
void cocis_sync_event_clear(uint32_t bits);
bool cocis_sync_event_check(uint32_t bits);
uint32_t cocis_sync_event_wait(uint32_t bits, bool clear_on_exit, bool wait_for_all, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* COCIS_SYNC_H */
