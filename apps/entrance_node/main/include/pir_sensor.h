#ifndef PIR_SENSOR_H
#define PIR_SENSOR_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t pir_sensor_init(void);

esp_err_t pir_sensor_simulate_motion(uint8_t room_id, bool active);

#ifdef __cplusplus
}
#endif

#endif /* PIR_SENSOR_H */
