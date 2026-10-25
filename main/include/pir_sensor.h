#ifndef PIR_SENSOR_H
#define PIR_SENSOR_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t pir_sensor_init(void);
esp_err_t pir_sensor_simulate_motion(uint8_t room_id, bool motion_active);
bool      pir_sensor_get_state(uint8_t room_id);

#endif
