#ifndef PIR_SENSOR_H
#define PIR_SENSOR_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pir_motion_cb_t)(bool motion_active);

esp_err_t pir_sensor_init(gpio_num_t pin, pir_motion_cb_t cb);

bool pir_sensor_is_motion_active(void);

void pir_sensor_simulate(bool active);

#ifdef __cplusplus
}
#endif

#endif /* PIR_SENSOR_H */
