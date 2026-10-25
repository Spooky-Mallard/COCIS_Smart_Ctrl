#ifndef COCIS_TYPES_H
#define COCIS_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NODE_ID_ENTRANCE  = 0,
    NODE_ID_ROOM_1    = 1,
    NODE_ID_ROOM_2    = 2,
    NODE_ID_COUNT     = 3,
    NODE_ID_BROADCAST = 255
} node_id_t;

typedef enum {
    PROTO_MSG_HEARTBEAT      = 0x01,
    PROTO_MSG_ROOM_EVENT     = 0x02,
    PROTO_MSG_SCHEDULE_SYNC  = 0x03,
    PROTO_MSG_EMERGENCY      = 0x04,
    PROTO_MSG_LIGHT_OVERRIDE = 0x05,
    PROTO_MSG_ACK            = 0xF0
} proto_msg_type_t;

typedef enum {
    LIGHT_STATE_OFF_VACANT   = 0,
    LIGHT_STATE_ON_ACTIVE    = 1,
    LIGHT_STATE_ON_GRACE_HOLD = 2
} light_state_t;

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
    char     hmac[65];
} proto_packet_t;

typedef struct {
    node_id_t   node_id;
    const char *name;
    bool        link_simulated_online;
    bool        heartbeat_healthy;
    uint32_t    last_tx_seq;
    uint32_t    last_rx_seq;
    uint32_t    acked_count;
    uint32_t    retry_count;
    uint32_t    dup_suppressed_count;
    uint32_t    buffered_offline_count;
    uint32_t    replayed_count;
    int64_t     last_seen_uptime_sec;
    int8_t      rssi;
} node_net_status_t;

#ifdef __cplusplus
}
#endif

#endif /* COCIS_TYPES_H */
