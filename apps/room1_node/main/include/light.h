#ifndef LIGHT_H
#define LIGHT_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "cocis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*light_state_change_cb_t)(light_state_t new_state, bool occupied, uint32_t on_sec, uint32_t saved_mwh);

esp_err_t light_init(gpio_num_t relay_pin, light_state_change_cb_t cb);

void light_handle_motion(bool motion_active);

void light_tick_1hz(void);

void light_set_override(uint8_t override_val);

light_state_t light_get_state(void);

bool light_is_relay_on(void);

uint32_t light_get_on_seconds(void);

uint32_t light_get_energy_saved_mwh(void);

const char *light_state_to_str(light_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* LIGHT_H */
