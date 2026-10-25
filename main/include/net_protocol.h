#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    NODE_ID_ENTRANCE = 0,
    NODE_ID_ROOM_1 = 1,
    NODE_ID_ROOM_2 = 2,
    NODE_ID_COUNT = 3,
    NODE_ID_BROADCAST = 255
} node_id_t;

typedef enum {
    PROTO_MSG_HEARTBEAT = 1,
    PROTO_MSG_ROOM_EVENT = 2,
    PROTO_MSG_SCHEDULE_SYNC = 3,
    PROTO_MSG_EMERGENCY = 4,
    PROTO_MSG_ACK = 240
} proto_msg_type_t;

typedef struct {
    uint8_t  version;
    uint8_t  msg_type;
    uint8_t  src_node;
    uint8_t  dst_node;
    uint32_t seq_id;
    uint32_t ack_seq_id;
    int64_t  timestamp_epoch;
    uint8_t  room_id;
    uint8_t  occupied;
    uint8_t  light_state;
    uint8_t  emergency_active;
    uint32_t light_on_sec;
    uint32_t energy_saved_mwh;
    uint32_t uptime_sec;
} proto_packet_t;

typedef struct {
    node_id_t node_id;
    const char *name;
    bool      link_simulated_online;
    bool      heartbeat_healthy;
    uint32_t  last_tx_seq;
    uint32_t  last_rx_seq;
    uint32_t  acked_count;
    uint32_t  retry_count;
    uint32_t  dup_suppressed_count;
    uint32_t  buffered_offline_count;
    uint32_t  replayed_count;
    int64_t   last_seen_uptime_sec;
} node_net_status_t;

esp_err_t net_protocol_init(void);
esp_err_t net_protocol_send_room_event(uint8_t room_id, bool occupied, uint8_t light_state,
                                       uint32_t light_on_sec, uint32_t energy_saved_mwh);
esp_err_t net_protocol_broadcast_emergency(bool emergency_active);
esp_err_t net_protocol_broadcast_schedule_sync(void);
esp_err_t net_protocol_set_node_online(node_id_t node, bool online);
esp_err_t net_protocol_get_node_status(node_id_t node, node_net_status_t *out_status);
bool      net_protocol_is_mqtt_connected(void);

#endif
