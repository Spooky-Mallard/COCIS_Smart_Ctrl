#include "net_protocol.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cocis_mqtt.h"
#include "cocis_protocol.h"
#include "cocis_crypto.h"
#include "cocis_health.h"
#include "cocis_time.h"
#include "cocis_sync.h"
#include "logger.h"
#include "occupancy_ledger.h"
#include "display.h"

static const char *TAG = "ENTRANCE_PROTO";

static entrance_room_telemetry_t s_rooms[3];
static bool s_initialized = false;

static void on_room_telemetry(const char *topic, const char *payload)
{
    (void)topic;
    proto_packet_t pkt;
    esp_err_t err = cocis_proto_deserialize(payload, CFG_SECRET_KEY, &pkt);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Rejected invalid or tampered packet: %s", payload);
        return;
    }

    if (pkt.src_node >= 3 || pkt.room_id > 2) {
        return;
    }

    uint8_t rid = (pkt.room_id > 0) ? pkt.room_id : pkt.src_node;
    s_rooms[rid].node_id = (node_id_t)pkt.src_node;
    s_rooms[rid].last_seen_uptime_sec = cocis_uptime_sec();
    s_rooms[rid].heartbeat_healthy = true;
    s_rooms[rid].last_room_occupied = pkt.occupied;
    s_rooms[rid].last_light_state = pkt.light_state;

    cocis_health_record_rx(pkt.src_node, pkt.seq_id, pkt.uptime_sec, -55);

    if (pkt.msg_type == PROTO_MSG_HEARTBEAT) {
        return;
    }

    if (pkt.msg_type == PROTO_MSG_ROOM_EVENT) {
        /* Anti-replay & idempotent duplicate suppression */
        if (cocis_proto_is_duplicate(pkt.src_node, pkt.seq_id)) {
            s_rooms[rid].dup_suppressed_count++;
            ESP_LOGW(TAG, "Suppressed duplicate event seq=%lu from Node %u",
                     (unsigned long)pkt.seq_id, (unsigned)pkt.src_node);
        } else {
            cocis_proto_record_seq(pkt.src_node, pkt.seq_id);
            s_rooms[rid].last_rx_seq = pkt.seq_id;
            s_rooms[rid].acked_count++;

            char detail[64];
            snprintf(detail, sizeof(detail), "OCC=%u LGT=%u ON=%lus",
                     (unsigned)pkt.occupied, (unsigned)pkt.light_state, (unsigned long)pkt.light_on_sec);
            logger_log_lighting(rid, detail, (int32_t)pkt.energy_saved_mwh);

            char l0[17], l1[17];
            snprintf(l0, sizeof(l0), "ROOM %u UPDATE!", rid);
            snprintf(l1, sizeof(l1), "R%u:%s L%u:%s",
                     rid, pkt.occupied ? "OCC" : "VAC",
                     rid, (pkt.light_state != 0) ? "ON " : "OFF");
            display_show_alert(l0, l1, 2500);

            ESP_LOGI(TAG, ">> [INGRESS FROM ROOM %u] Occupancy=%s | Lighting=%s | Saved=%lu mWh",
                     rid, pkt.occupied ? "OCCUPIED" : "VACANT",
                     (pkt.light_state != 0) ? "ON" : "OFF", (unsigned long)pkt.energy_saved_mwh);
        }

        /* Send ACK back to the room */
        proto_packet_t ack_pkt = {
            .version = 1,
            .msg_type = PROTO_MSG_ACK,
            .src_node = NODE_ID_ENTRANCE,
            .dst_node = pkt.src_node,
            .seq_id = cocis_proto_next_seq(NODE_ID_ENTRANCE),
            .ack_seq_id = pkt.seq_id,
            .timestamp_epoch = cocis_time_now_epoch(),
            .room_id = rid,
            .uptime_sec = cocis_uptime_sec()
        };

        char ack_buf[256];
        if (cocis_proto_serialize(&ack_pkt, CFG_SECRET_KEY, ack_buf, sizeof(ack_buf)) == ESP_OK) {
            char ctrl_topic[64];
            snprintf(ctrl_topic, sizeof(ctrl_topic), "%s/room/%u/control", CFG_MQTT_TOPIC_PREFIX, pkt.src_node);
            cocis_mqtt_publish(ctrl_topic, ack_buf);
        }
    }
}

esp_err_t net_protocol_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    cocis_proto_init();

    s_rooms[1].node_id = NODE_ID_ROOM_1;
    s_rooms[1].name = "Lecture Room 1";
    s_rooms[1].heartbeat_healthy = false;

    s_rooms[2].node_id = NODE_ID_ROOM_2;
    s_rooms[2].name = "Lecture Room 2";
    s_rooms[2].heartbeat_healthy = false;

    /* Subscribe to all room telemetry topics (both explicit and wildcard) */
    char sub_pattern[64], sub_r1[64], sub_r2[64];
    snprintf(sub_pattern, sizeof(sub_pattern), "%s/room/+/telemetry", CFG_MQTT_TOPIC_PREFIX);
    snprintf(sub_r1, sizeof(sub_r1), "%s/room/1/telemetry", CFG_MQTT_TOPIC_PREFIX);
    snprintf(sub_r2, sizeof(sub_r2), "%s/room/2/telemetry", CFG_MQTT_TOPIC_PREFIX);
    cocis_mqtt_subscribe(sub_r1, on_room_telemetry);
    cocis_mqtt_subscribe(sub_r2, on_room_telemetry);
    cocis_mqtt_subscribe(sub_pattern, on_room_telemetry);

    s_initialized = true;
    ESP_LOGI(TAG, "Entrance Hub network protocol initialized on topic %s", sub_pattern);
    return ESP_OK;
}

esp_err_t net_protocol_send_override(uint8_t room_id, uint8_t state)
{
    if (room_id < 1 || room_id > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    proto_packet_t pkt = {
        .version = 1,
        .msg_type = PROTO_MSG_LIGHT_OVERRIDE,
        .src_node = NODE_ID_ENTRANCE,
        .dst_node = room_id,
        .seq_id = cocis_proto_next_seq(NODE_ID_ENTRANCE),
        .timestamp_epoch = cocis_time_now_epoch(),
        .room_id = room_id,
        .light_state = state,
        .uptime_sec = cocis_uptime_sec()
    };

    char buf[256];
    esp_err_t err = cocis_proto_serialize(&pkt, CFG_SECRET_KEY, buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/room/%u/control", CFG_MQTT_TOPIC_PREFIX, room_id);
    return cocis_mqtt_publish(topic, buf);
}

esp_err_t net_protocol_send_emergency(bool active)
{
    proto_packet_t pkt = {
        .version = 1,
        .msg_type = PROTO_MSG_EMERGENCY,
        .src_node = NODE_ID_ENTRANCE,
        .dst_node = NODE_ID_BROADCAST,
        .seq_id = cocis_proto_next_seq(NODE_ID_ENTRANCE),
        .timestamp_epoch = cocis_time_now_epoch(),
        .emergency_active = active ? 1 : 0,
        .uptime_sec = cocis_uptime_sec()
    };

    char buf[256];
    esp_err_t err = cocis_proto_serialize(&pkt, CFG_SECRET_KEY, buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/broadcast", CFG_MQTT_TOPIC_PREFIX);
    return cocis_mqtt_publish(topic, buf);
}

esp_err_t net_protocol_send_schedule_sync(void)
{
    proto_packet_t pkt = {
        .version = 1,
        .msg_type = PROTO_MSG_SCHEDULE_SYNC,
        .src_node = NODE_ID_ENTRANCE,
        .dst_node = NODE_ID_BROADCAST,
        .seq_id = cocis_proto_next_seq(NODE_ID_ENTRANCE),
        .timestamp_epoch = cocis_time_now_epoch(),
        .uptime_sec = cocis_uptime_sec()
    };

    char buf[256];
    esp_err_t err = cocis_proto_serialize(&pkt, CFG_SECRET_KEY, buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/broadcast", CFG_MQTT_TOPIC_PREFIX);
    return cocis_mqtt_publish(topic, buf);
}

const entrance_room_telemetry_t *net_protocol_get_room_telemetry(uint8_t room_id)
{
    if (room_id < 1 || room_id > 2) {
        return NULL;
    }
    return &s_rooms[room_id];
}

esp_err_t net_protocol_get_status(node_id_t node_id, node_net_status_t *out_status)
{
    if (!out_status || node_id >= NODE_ID_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_status, 0, sizeof(node_net_status_t));
    out_status->node_id = node_id;
    if (node_id == NODE_ID_ENTRANCE) {
        out_status->name = "Main Entrance Hub";
        out_status->heartbeat_healthy = true;
        out_status->link_simulated_online = true;
        out_status->last_tx_seq = 10;
        out_status->acked_count = 10;
    } else {
        out_status->name = (node_id == NODE_ID_ROOM_1) ? "Lecture Room 1" : "Lecture Room 2";
        out_status->heartbeat_healthy = s_rooms[node_id].heartbeat_healthy;
        out_status->link_simulated_online = true;
        out_status->last_rx_seq = s_rooms[node_id].last_rx_seq;
        out_status->acked_count = s_rooms[node_id].acked_count;
        out_status->dup_suppressed_count = s_rooms[node_id].dup_suppressed_count;
        out_status->last_seen_uptime_sec = s_rooms[node_id].last_seen_uptime_sec;
        out_status->buffered_offline_count = 0;
        out_status->replayed_count = 2;
    }
    return ESP_OK;
}

esp_err_t net_protocol_set_node_online(node_id_t node_id, bool online)
{
    if (node_id < 1 || node_id > 2) {
        return ESP_ERR_INVALID_ARG;
    }
    s_rooms[node_id].heartbeat_healthy = online;
    return ESP_OK;
}

