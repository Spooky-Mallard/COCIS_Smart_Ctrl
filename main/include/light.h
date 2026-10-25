#ifndef LIGHT_H
#define LIGHT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    LIGHT_FSM_OFF_VACANT = 0,
    LIGHT_FSM_ON_ACTIVE = 1,
    LIGHT_FSM_ON_GRACE_HOLD = 2
} light_fsm_state_t;

typedef struct {
    uint8_t           room_id;
    bool              pir_motion_active;
    bool              relay_on;
    light_fsm_state_t fsm_state;
    uint32_t          seconds_until_off;
    uint32_t          total_light_on_sec;
    uint32_t          total_occupied_sec;
    uint32_t          total_vacant_off_sec;
    uint32_t          energy_saved_mwh;
    uint32_t          transition_count;
} room_light_status_t;

esp_err_t   light_init(void);
esp_err_t   light_notify_motion(uint8_t room_id, bool motion_detected);
void        light_tick_1hz(void);
esp_err_t   light_get_room_status(uint8_t room_id, room_light_status_t *out_status);
uint32_t    light_get_total_energy_saved_mwh(void);
const char *light_fsm_to_str(light_fsm_state_t state);

#endif
