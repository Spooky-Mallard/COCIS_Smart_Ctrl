#ifndef COCIS_HEALTH_H
#define COCIS_HEALTH_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "cocis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cocis_health_init(node_id_t my_node_id);

void cocis_health_record_rx(node_id_t src_node, uint32_t seq_id, uint32_t uptime_sec, int8_t rssi);

bool cocis_health_is_node_alive(node_id_t node_id);

const node_net_status_t *cocis_health_get_status(node_id_t node_id);

uint32_t cocis_uptime_sec(void);

#ifdef __cplusplus
}
#endif

#endif /* COCIS_HEALTH_H */
