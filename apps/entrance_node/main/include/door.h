#ifndef DOOR_H
#define DOOR_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    DOOR_ID_ENTRY = 0,
    DOOR_ID_EXIT = 1,
    DOOR_ID_COUNT = 2
} door_id_t;

typedef enum {
    DOOR_STATE_LOCKED = 0,
    DOOR_STATE_UNLOCKED_TIMED,
    DOOR_STATE_EMERGENCY_OPEN,
    DOOR_STATE_FORCED_ALARM
} door_state_t;

typedef struct {
    door_state_t state;
    uint32_t     open_count;
    uint32_t     forced_alarm_count;
} door_status_t;

esp_err_t   door_init(void);
esp_err_t   door_unlock_timed(door_id_t door);
esp_err_t   door_lock_now(door_id_t door);
esp_err_t   door_set_emergency_all(bool emergency_open);
esp_err_t   door_trigger_deny_indicator(void);
esp_err_t   door_simulate_forced_hold(door_id_t door);
esp_err_t   door_get_status(door_id_t door, door_status_t *out_status);
const char *door_state_to_str(door_state_t state);

#endif
