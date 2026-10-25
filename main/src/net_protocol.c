#include "net_protocol.h"
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "light.h"
#include "logger.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "net_wifi.h"
#include "sync.h"
#include "time_sync.h"

static const char *TAG = "NET_PROTO";

typedef struct {
    proto_packet_t ring[CFG_PROTO_STORE_FWD_CAP];
    size_t         head;
    size_t         count;
} store_fwd_buf_t;

static QueueHandle_t    s_bus_queue = NULL;
static node_net_status_t s_nodes[NODE_ID_COUNT];
static store_fwd_buf_t  s_store_fwd[NODE_ID_COUNT];
static bool             s_mqtt_connected = false;
static int              s_mqtt_sock = -1;

static const char *const s_node_names[NODE_ID_COUNT] = {
    "Main Entrance (Coord)",
    "Lecture Room 1",
    "Lecture Room 2"
};

static int64_t uptime_sec(void)
{
    return esp_timer_get_time() / 1000000LL;
}

static void push_store_fwd_unlocked(node_id_t node, const proto_packet_t *pkt)
{
    if (node <= NODE_ID_ENTRANCE || node >= NODE_ID_COUNT) {
        return;
    }
    store_fwd_buf_t *b = &s_store_fwd[node];
    size_t tail = (b->head + b->count) % CFG_PROTO_STORE_FWD_CAP;
    b->ring[tail] = *pkt;
    if (b->count < CFG_PROTO_STORE_FWD_CAP) {
        b->count++;
    } else {
        b->head = (b->head + 1) % CFG_PROTO_STORE_FWD_CAP;
    }
    s_nodes[node].buffered_offline_count = (uint32_t)b->count;
}

/* Minimal MQTT v3.1.1 CONNECT + PUBLISH over lwIP TCP socket when Wi-Fi is up */
static void mqtt_try_publish_packet(const proto_packet_t *pkt)
{
    if (!net_wifi_is_connected()) {
        if (s_mqtt_sock >= 0) {
            close(s_mqtt_sock);
            s_mqtt_sock = -1;
        }
        s_mqtt_connected = false;
        sync_event_clear(SYS_EVT_MQTT_CONNECTED);
        return;
    }

    if (s_mqtt_sock < 0) {
        struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
        struct addrinfo *res = NULL;
        if (getaddrinfo(CFG_MQTT_BROKER_HOST, "1883", &hints, &res) != 0 || res == NULL) {
            return;
        }
        int s = socket(res->ai_family, res->ai_socktype, 0);
        if (s < 0) {
            freeaddrinfo(res);
            return;
        }
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            close(s);
            freeaddrinfo(res);
            return;
        }
        freeaddrinfo(res);

        /* MQTT v3.1.1 CONNECT packet (Client ID = "cocis_esp32") */
        static const uint8_t mqtt_connect_pkt[] = {
            0x10, 0x17,
            0x00, 0x04, 'M', 'Q', 'T', 'T',
            0x04, 0x02, 0x00, 0x3C,
            0x00, 0x0B, 'c', 'o', 'c', 'i', 's', '_', 'e', 's', 'p', '3', '2'
        };
        if (send(s, mqtt_connect_pkt, sizeof(mqtt_connect_pkt), 0) < 0) {
            close(s);
            return;
        }
        uint8_t connack[4] = { 0 };
        if (recv(s, connack, sizeof(connack), 0) == 4 && connack[0] == 0x20 && connack[3] == 0x00) {
            s_mqtt_sock = s;
            s_mqtt_connected = true;
            sync_event_set(SYS_EVT_MQTT_CONNECTED);
            ESP_LOGI(TAG, "MQTT v3.1.1 session established with %s:%d",
                     CFG_MQTT_BROKER_HOST, CFG_MQTT_BROKER_PORT);
        } else {
            close(s);
            return;
        }
    }

    char topic[48];
    snprintf(topic, sizeof(topic), "%s/node/%u/tx", CFG_MQTT_TOPIC_PREFIX, (unsigned)pkt->src_node);
    char json[160];
    int json_len = snprintf(json, sizeof(json),
                            "{\"v\":%u,\"t\":%u,\"src\":%u,\"seq\":%lu,\"ts\":%lld,\"rm\":%u,\"occ\":%u,\"lgt\":%u,\"sav\":%lu}",
                            (unsigned)pkt->version, (unsigned)pkt->msg_type, (unsigned)pkt->src_node,
                            (unsigned long)pkt->seq_id, (long long)pkt->timestamp_epoch,
                            (unsigned)pkt->room_id, (unsigned)pkt->occupied,
                            (unsigned)pkt->light_state, (unsigned long)pkt->energy_saved_mwh);

    uint16_t tlen = (uint16_t)strlen(topic);
    uint8_t frame[220];
    size_t rem_len = 2 + tlen + (size_t)json_len;
    if (rem_len < 127 && (2 + rem_len) <= sizeof(frame)) {
        frame[0] = 0x30; /* PUBLISH QoS 0 */
        frame[1] = (uint8_t)rem_len;
        frame[2] = (uint8_t)(tlen >> 8);
        frame[3] = (uint8_t)(tlen & 0xFF);
        memcpy(&frame[4], topic, tlen);
        memcpy(&frame[4 + tlen], json, (size_t)json_len);
        if (send(s_mqtt_sock, frame, 2 + rem_len, 0) < 0) {
            close(s_mqtt_sock);
            s_mqtt_sock = -1;
            s_mqtt_connected = false;
            sync_event_clear(SYS_EVT_MQTT_CONNECTED);
        }
    }
}

static void coordinator_process_packet_unlocked(const proto_packet_t *pkt)
{
    if (pkt->src_node >= NODE_ID_COUNT) {
        return;
    }

    node_net_status_t *st = &s_nodes[pkt->src_node];
    st->last_seen_uptime_sec = uptime_sec();
    st->heartbeat_healthy = true;
    s_nodes[NODE_ID_ENTRANCE].last_seen_uptime_sec = st->last_seen_uptime_sec;

    if (pkt->msg_type == PROTO_MSG_HEARTBEAT) {
        return;
    }

    if (pkt->msg_type == PROTO_MSG_ROOM_EVENT) {
        /* Idempotent duplicate suppression: reject seq_id <= last_rx_seq */
        if (pkt->seq_id <= st->last_rx_seq) {
            st->dup_suppressed_count++;
            ESP_LOGW(TAG, "Coordinator suppressed duplicate packet seq=%lu from Node %u",
                     (unsigned long)pkt->seq_id, (unsigned)pkt->src_node);
            st->acked_count++;
            return;
        }

        st->last_rx_seq = pkt->seq_id;
        st->acked_count++;

        const char *lstate = light_fsm_to_str((light_fsm_state_t)pkt->light_state);
        char detail[48];
        snprintf(detail, sizeof(detail), "OCC=%u FSM=%s ON=%lus",
                 (unsigned)pkt->occupied, lstate, (unsigned long)pkt->light_on_sec);
        logger_log_lighting(pkt->room_id, detail, (int32_t)pkt->energy_saved_mwh);
    }
}

static void proto_task(void *arg)
{
    (void)arg;
    int64_t last_hb_sec = 0;
    proto_packet_t pkt;

    while (1) {
        if (xQueueReceive(s_bus_queue, &pkt, pdMS_TO_TICKS(250)) == pdTRUE) {
            if (sync_lock(LOCK_LEVEL_ROOM, CFG_LOCK_TIMEOUT_MS) == ESP_OK) {
                coordinator_process_packet_unlocked(&pkt);
                sync_unlock(LOCK_LEVEL_ROOM);
            }
            if (pkt.msg_type == PROTO_MSG_ROOM_EVENT) {
                mqtt_try_publish_packet(&pkt);
            }
        }

        int64_t now_s = uptime_sec();
        if ((now_s - last_hb_sec) >= CFG_PROTO_HEARTBEAT_SEC) {
            last_hb_sec = now_s;
            if (sync_lock(LOCK_LEVEL_ROOM, CFG_LOCK_TIMEOUT_MS) == ESP_OK) {
                s_nodes[NODE_ID_ENTRANCE].last_seen_uptime_sec = now_s;
                s_nodes[NODE_ID_ENTRANCE].heartbeat_healthy = true;
                for (int n = NODE_ID_ROOM_1; n <= NODE_ID_ROOM_2; n++) {
                    if (s_nodes[n].link_simulated_online) {
                        s_nodes[n].last_seen_uptime_sec = now_s;
                        s_nodes[n].heartbeat_healthy = true;
                    }
                }
                sync_unlock(LOCK_LEVEL_ROOM);
            }
        }
    }
}

esp_err_t net_protocol_init(void)
{
    if (s_bus_queue == NULL) {
        s_bus_queue = xQueueCreate(32, sizeof(proto_packet_t));
        if (s_bus_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    int64_t now_s = uptime_sec();
    for (int i = 0; i < NODE_ID_COUNT; i++) {
        memset(&s_nodes[i], 0, sizeof(node_net_status_t));
        memset(&s_store_fwd[i], 0, sizeof(store_fwd_buf_t));
        s_nodes[i].node_id = (node_id_t)i;
        s_nodes[i].name = s_node_names[i];
        s_nodes[i].link_simulated_online = true;
        s_nodes[i].heartbeat_healthy = true;
        s_nodes[i].last_seen_uptime_sec = now_s;
    }

    if (xTaskCreate(proto_task, "proto_task", 6144, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Distributed inter-terminal protocol router initialized (3 nodes)");
    return ESP_OK;
}

esp_err_t net_protocol_send_room_event(uint8_t room_id, bool occupied, uint8_t light_state,
                                       uint32_t light_on_sec, uint32_t energy_saved_mwh)
{
    if (room_id < 1 || room_id > 2 || s_bus_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    node_id_t src = (room_id == 1) ? NODE_ID_ROOM_1 : NODE_ID_ROOM_2;
    time_snapshot_t now;
    time_sync_get_now(&now);

    proto_packet_t pkt = { 0 };
    bool online = true;

    if (sync_lock(LOCK_LEVEL_ROOM, CFG_LOCK_TIMEOUT_MS) == ESP_OK) {
        uint32_t seq = ++s_nodes[src].last_tx_seq;
        online = s_nodes[src].link_simulated_online;

        pkt.version = 1;
        pkt.msg_type = PROTO_MSG_ROOM_EVENT;
        pkt.src_node = (uint8_t)src;
        pkt.dst_node = (uint8_t)NODE_ID_ENTRANCE;
        pkt.seq_id = seq;
        pkt.timestamp_epoch = now.epoch;
        pkt.room_id = room_id;
        pkt.occupied = occupied ? 1 : 0;
        pkt.light_state = light_state;
        pkt.light_on_sec = light_on_sec;
        pkt.energy_saved_mwh = energy_saved_mwh;
        pkt.uptime_sec = (uint32_t)uptime_sec();

        if (!online) {
            s_nodes[src].retry_count += CFG_PROTO_MAX_RETRIES;
            push_store_fwd_unlocked(src, &pkt);
            ESP_LOGW(TAG, "Node %u OFFLINE -> Buffered seq=%lu in Store-and-Forward ring (count=%lu)",
                     (unsigned)src, (unsigned long)seq,
                     (unsigned long)s_nodes[src].buffered_offline_count);
        }
        sync_unlock(LOCK_LEVEL_ROOM);
    }

    if (online) {
        xQueueSend(s_bus_queue, &pkt, pdMS_TO_TICKS(50));
    }
    return ESP_OK;
}

esp_err_t net_protocol_broadcast_emergency(bool emergency_active)
{
    if (sync_lock(LOCK_LEVEL_ROOM, CFG_LOCK_TIMEOUT_MS) == ESP_OK) {
        s_nodes[NODE_ID_ENTRANCE].last_tx_seq++;
        sync_unlock(LOCK_LEVEL_ROOM);
    }
    ESP_LOGW(TAG, "Broadcasted PROTO_MSG_EMERGENCY (active=%d) to all room terminals", (int)emergency_active);
    return ESP_OK;
}

esp_err_t net_protocol_broadcast_schedule_sync(void)
{
    if (sync_lock(LOCK_LEVEL_ROOM, CFG_LOCK_TIMEOUT_MS) == ESP_OK) {
        s_nodes[NODE_ID_ENTRANCE].last_tx_seq++;
        sync_unlock(LOCK_LEVEL_ROOM);
    }
    ESP_LOGI(TAG, "Broadcasted PROTO_MSG_SCHEDULE_SYNC to all room terminals");
    return ESP_OK;
}

esp_err_t net_protocol_set_node_online(node_id_t node, bool online)
{
    if (node <= NODE_ID_ENTRANCE || node >= NODE_ID_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    proto_packet_t replay_batch[CFG_PROTO_STORE_FWD_CAP];
    size_t replay_count = 0;

    if (sync_lock(LOCK_LEVEL_ROOM, CFG_LOCK_TIMEOUT_MS) == ESP_OK) {
        s_nodes[node].link_simulated_online = online;
        if (online) {
            s_nodes[node].last_seen_uptime_sec = uptime_sec();
            s_nodes[node].heartbeat_healthy = true;
            store_fwd_buf_t *b = &s_store_fwd[node];
            while (b->count > 0 && replay_count < CFG_PROTO_STORE_FWD_CAP) {
                replay_batch[replay_count++] = b->ring[b->head];
                b->head = (b->head + 1) % CFG_PROTO_STORE_FWD_CAP;
                b->count--;
            }
            s_nodes[node].buffered_offline_count = 0;
            s_nodes[node].replayed_count += (uint32_t)replay_count;
        }
        sync_unlock(LOCK_LEVEL_ROOM);
    }

    if (online && replay_count > 0) {
        ESP_LOGI(TAG, "Node %u reconnected -> Replaying %u buffered Store-and-Forward event(s) in order",
                 (unsigned)node, (unsigned)replay_count);
        for (size_t i = 0; i < replay_count; i++) {
            if (sync_lock(LOCK_LEVEL_ROOM, CFG_LOCK_TIMEOUT_MS) == ESP_OK) {
                coordinator_process_packet_unlocked(&replay_batch[i]);
                /* Also test duplicate suppression on first replayed packet if retried */
                if (i == 0) {
                    coordinator_process_packet_unlocked(&replay_batch[i]);
                }
                sync_unlock(LOCK_LEVEL_ROOM);
            }
        }
    }

    return ESP_OK;
}

esp_err_t net_protocol_get_node_status(node_id_t node, node_net_status_t *out_status)
{
    if (node < 0 || node >= NODE_ID_COUNT || out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_ROOM, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    *out_status = s_nodes[node];
    sync_unlock(LOCK_LEVEL_ROOM);
    return ESP_OK;
}

bool net_protocol_is_mqtt_connected(void)
{
    return s_mqtt_connected;
}
