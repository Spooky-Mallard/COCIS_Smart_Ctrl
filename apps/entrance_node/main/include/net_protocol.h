#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "cocis_types.h"
#include "cocis_config.h"
#include "cocis_mqtt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    node_id_t   node_id;
    const char *name;
    bool        heartbeat_healthy;
    uint32_t    last_rx_seq;
    uint32_t    acked_count;
    uint32_t    dup_suppressed_count;
    int64_t     last_seen_uptime_sec;
    int8_t      rssi;
    uint8_t     last_room_occupied;
    uint8_t     last_light_state;
} entrance_room_telemetry_t;

esp_err_t net_protocol_init(void);

esp_err_t net_protocol_send_override(uint8_t room_id, uint8_t state);

esp_err_t net_protocol_send_emergency(bool active);

esp_err_t net_protocol_send_schedule_sync(void);

esp_err_t net_protocol_get_status(node_id_t node_id, node_net_status_t *out_status);

static inline esp_err_t net_protocol_get_node_status(node_id_t node_id, node_net_status_t *out_status) {
    return net_protocol_get_status(node_id, out_status);
}

static inline bool net_protocol_is_mqtt_connected(void) {
    return cocis_mqtt_is_connected();
}

esp_err_t net_protocol_set_node_online(node_id_t node_id, bool online);

static inline esp_err_t net_protocol_broadcast_emergency(bool active) {
    return net_protocol_send_emergency(active);
}

static inline esp_err_t net_protocol_broadcast_schedule_sync(void) {
    return net_protocol_send_schedule_sync();
}

const entrance_room_telemetry_t *net_protocol_get_room_telemetry(uint8_t room_id);

#ifdef __cplusplus
}
#endif

#endif /* NET_PROTOCOL_H */
