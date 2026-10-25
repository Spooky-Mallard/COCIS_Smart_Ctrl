#ifndef COCIS_MQTT_H
#define COCIS_MQTT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cocis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*cocis_mqtt_rx_cb_t)(const char *topic, const char *payload);

esp_err_t cocis_mqtt_init(node_id_t my_node_id);

bool cocis_mqtt_is_connected(void);

esp_err_t cocis_mqtt_publish(const char *topic, const char *payload);

esp_err_t cocis_mqtt_subscribe(const char *topic, cocis_mqtt_rx_cb_t callback);

#ifdef __cplusplus
}
#endif

#endif /* COCIS_MQTT_H */
