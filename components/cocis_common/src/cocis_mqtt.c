#include "cocis_mqtt.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "cocis_config.h"
#include "cocis_sync.h"
#include "cocis_wifi.h"

static const char *TAG = "cocis_mqtt";

#define MAX_SUBSCRIPTIONS 8
typedef struct {
    char topic[64];
    cocis_mqtt_rx_cb_t cb;
} sub_entry_t;

static sub_entry_t s_subs[MAX_SUBSCRIPTIONS];
static size_t s_sub_count = 0;

static int s_sock = -1;
static bool s_connected = false;
static node_id_t s_my_node_id = NODE_ID_ENTRANCE;
static char s_client_id[32] = { 0 };
static uint8_t s_rx_buf[1536];
static size_t s_rx_buf_len = 0;
static uint16_t s_sub_pkt_id = 1;
static int64_t s_last_ping_sec = 0;

static esp_err_t mqtt_send_bytes(const uint8_t *data, size_t len)
{
    if (s_sock < 0) {
        return ESP_FAIL;
    }
    size_t sent = 0;
    while (sent < len) {
        int r = send(s_sock, data + sent, len - sent, 0);
        if (r <= 0) {
            ESP_LOGW(TAG, "Socket send error: %d (errno=%d)", r, errno);
            return ESP_FAIL;
        }
        sent += (size_t)r;
    }
    return ESP_OK;
}

static esp_err_t mqtt_subscribe_remote(const char *topic)
{
    if (s_sock < 0 || !s_connected) {
        return ESP_FAIL;
    }

    uint16_t tlen = (uint16_t)strlen(topic);
    uint8_t pkt[128];
    size_t rem_len = 2 + 2 + tlen + 1; /* Packet ID (2) + Topic Len (2) + Topic + QoS (1) */

    uint16_t pid = s_sub_pkt_id++;
    if (s_sub_pkt_id == 0) s_sub_pkt_id = 1;

    pkt[0] = 0x82; /* SUBSCRIBE QoS 1 */
    pkt[1] = (uint8_t)rem_len;
    pkt[2] = (uint8_t)(pid >> 8);
    pkt[3] = (uint8_t)(pid & 0xFF);
    pkt[4] = (uint8_t)(tlen >> 8);
    pkt[5] = (uint8_t)(tlen & 0xFF);
    memcpy(&pkt[6], topic, tlen);
    pkt[6 + tlen] = 0x00; /* Requested QoS 0 */

    if (mqtt_send_bytes(pkt, 7 + tlen) == ESP_OK) {
        ESP_LOGI(TAG, "Subscribed to MQTT topic: %s (pkt_id=%u)", topic, (unsigned)pid);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static bool topic_matches(const char *sub, const char *topic)
{
    if (!sub || !topic) {
        return false;
    }
    if (strcmp(sub, topic) == 0) {
        return true;
    }

    /* Multi-level wildcard '#' */
    const char *hash = strstr(sub, "/#");
    if (hash) {
        size_t prefix_len = (size_t)(hash - sub);
        if (strncmp(sub, topic, prefix_len) == 0) {
            return true;
        }
    }

    /* Single-level wildcard '+' */
    const char *plus = strstr(sub, "/+");
    if (plus) {
        size_t prefix_len = (size_t)(plus - sub);
        if (strncmp(sub, topic, prefix_len) != 0) {
            return false;
        }
        const char *sub_suffix = plus + 2;
        if (*sub_suffix == '\0') {
            return true;
        }
        const char *topic_rem = topic + prefix_len + 1;
        const char *next_slash = strchr(topic_rem, '/');
        if (next_slash && strcmp(next_slash, sub_suffix) == 0) {
            return true;
        }
    }
    return false;
}

static void mqtt_dispatch_publish(const char *topic, const char *payload)
{
    for (size_t i = 0; i < s_sub_count; i++) {
        if (topic_matches(s_subs[i].topic, topic) && s_subs[i].cb) {
            s_subs[i].cb(topic, payload);
        }
    }
}

static void mqtt_task(void *arg)
{
    (void)arg;

    while (1) {
        if (!cocis_wifi_is_connected()) {
            if (s_sock >= 0) {
                close(s_sock);
                s_sock = -1;
            }
            s_connected = false;
            s_rx_buf_len = 0;
            cocis_sync_event_clear(COCIS_EVT_MQTT_CONNECTED);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (s_sock < 0) {
            struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
            struct addrinfo *res = NULL;
            char port_str[8];
            snprintf(port_str, sizeof(port_str), "%d", CFG_MQTT_BROKER_PORT);

            if (getaddrinfo(CFG_MQTT_BROKER_HOST, port_str, &hints, &res) != 0 || !res) {
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            int s = socket(res->ai_family, res->ai_socktype, 0);
            if (s < 0) {
                freeaddrinfo(res);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
                close(s);
                freeaddrinfo(res);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            freeaddrinfo(res);

            /* Send CONNECT */
            uint16_t cid_len = (uint16_t)strlen(s_client_id);
            uint8_t conn_pkt[128];
            size_t rem_len = 10 + 2 + cid_len;

            conn_pkt[0] = 0x10; /* CONNECT */
            conn_pkt[1] = (uint8_t)rem_len;
            conn_pkt[2] = 0x00; conn_pkt[3] = 0x04;
            conn_pkt[4] = 'M';  conn_pkt[5] = 'Q'; conn_pkt[6] = 'T'; conn_pkt[7] = 'T';
            conn_pkt[8] = 0x04; /* Level 4 (v3.1.1) */
            conn_pkt[9] = 0x02; /* Clean Session */
            conn_pkt[10] = 0x00; conn_pkt[11] = 0x3C; /* 60s Keep Alive */
            conn_pkt[12] = (uint8_t)(cid_len >> 8);
            conn_pkt[13] = (uint8_t)(cid_len & 0xFF);
            memcpy(&conn_pkt[14], s_client_id, cid_len);

            if (send(s, conn_pkt, 14 + cid_len, 0) < 0) {
                close(s);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            /* Wait for CONNACK */
            uint8_t connack[4] = { 0 };
            int n = recv(s, connack, sizeof(connack), 0);
            if (n == 4 && connack[0] == 0x20 && connack[3] == 0x00) {
                s_sock = s;
                s_connected = true;
                s_rx_buf_len = 0;
                s_last_ping_sec = (int64_t)(esp_timer_get_time() / 1000000ULL);
                cocis_sync_event_set(COCIS_EVT_MQTT_CONNECTED);
                ESP_LOGI(TAG, "MQTT connected to %s:%d (Client: %s)",
                         CFG_MQTT_BROKER_HOST, CFG_MQTT_BROKER_PORT, s_client_id);

                /* Resubscribe to all registered topics */
                for (size_t i = 0; i < s_sub_count; i++) {
                    mqtt_subscribe_remote(s_subs[i].topic);
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            } else {
                ESP_LOGW(TAG, "CONNACK rejected or timed out (n=%d)", n);
                close(s);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
        }

        /* Periodic Keep-Alive PINGREQ every 20 seconds */
        int64_t now_sec = (int64_t)(esp_timer_get_time() / 1000000ULL);
        if (s_connected && (now_sec - s_last_ping_sec >= 20)) {
            s_last_ping_sec = now_sec;
            const uint8_t ping_pkt[2] = { 0xC0, 0x00 };
            if (mqtt_send_bytes(ping_pkt, 2) != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send MQTT PINGREQ, resetting connection");
                close(s_sock);
                s_sock = -1;
                s_connected = false;
                s_rx_buf_len = 0;
                cocis_sync_event_clear(COCIS_EVT_MQTT_CONNECTED);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
        }

        /* Read incoming frames into stream buffer */
        int r = recv(s_sock, s_rx_buf + s_rx_buf_len, sizeof(s_rx_buf) - 1 - s_rx_buf_len, 0);
        if (r > 0) {
            s_rx_buf_len += (size_t)r;
            s_rx_buf[s_rx_buf_len] = '\0';
        } else if (r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            ESP_LOGW(TAG, "MQTT connection lost (r=%d, errno=%d), reconnecting...", r, errno);
            close(s_sock);
            s_sock = -1;
            s_connected = false;
            s_rx_buf_len = 0;
            cocis_sync_event_clear(COCIS_EVT_MQTT_CONNECTED);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* Process all complete packets in the buffer */
        while (s_rx_buf_len >= 2) {
            uint8_t pkt_type = s_rx_buf[0] & 0xF0;

            /* Decode remaining length */
            size_t idx = 1;
            uint32_t rem_len = 0;
            uint32_t multiplier = 1;
            bool rem_len_complete = false;

            while (idx < s_rx_buf_len && idx <= 4) {
                uint8_t b = s_rx_buf[idx++];
                rem_len += (uint32_t)(b & 0x7F) * multiplier;
                multiplier *= 128;
                if ((b & 0x80) == 0) {
                    rem_len_complete = true;
                    break;
                }
            }

            if (!rem_len_complete) {
                break; /* Length not fully received yet */
            }

            size_t total_pkt_len = idx + rem_len;
            if (s_rx_buf_len < total_pkt_len) {
                break; /* Packet body not fully received yet */
            }

            if (pkt_type == 0x30) { /* PUBLISH */
                size_t v_idx = idx;
                if (v_idx + 2 <= total_pkt_len) {
                    uint16_t tlen = ((uint16_t)s_rx_buf[v_idx] << 8) | s_rx_buf[v_idx + 1];
                    v_idx += 2;
                    if (v_idx + tlen <= total_pkt_len) {
                        char topic[64];
                        size_t cpy_tlen = (tlen < sizeof(topic) - 1) ? tlen : (sizeof(topic) - 1);
                        memcpy(topic, &s_rx_buf[v_idx], cpy_tlen);
                        topic[cpy_tlen] = '\0';
                        v_idx += tlen;

                        uint8_t qos = (s_rx_buf[0] & 0x06) >> 1;
                        if (qos > 0 && v_idx + 2 <= total_pkt_len) {
                            v_idx += 2; /* Skip Packet ID */
                        }

                        size_t payload_len = (total_pkt_len > v_idx) ? (total_pkt_len - v_idx) : 0;
                        char rx_payload[512];
                        size_t cpy_plen = (payload_len < sizeof(rx_payload) - 1) ? payload_len : (sizeof(rx_payload) - 1);
                        if (cpy_plen > 0) {
                            memcpy(rx_payload, &s_rx_buf[v_idx], cpy_plen);
                        }
                        rx_payload[cpy_plen] = '\0';

                        ESP_LOGI(TAG, "MQTT Rx: topic='%s' payload_len=%u", topic, (unsigned)cpy_plen);
                        mqtt_dispatch_publish(topic, rx_payload);
                    }
                }
            } else if (pkt_type == 0x90) { /* SUBACK */
                ESP_LOGD(TAG, "SUBACK received from broker");
            } else if (pkt_type == 0xD0) { /* PINGRESP */
                ESP_LOGD(TAG, "PINGRESP received from broker");
            }

            /* Shift buffer forward past the consumed packet */
            size_t remaining = s_rx_buf_len - total_pkt_len;
            if (remaining > 0) {
                memmove(s_rx_buf, s_rx_buf + total_pkt_len, remaining);
            }
            s_rx_buf_len = remaining;
        }

        if (s_rx_buf_len >= sizeof(s_rx_buf) - 1) {
            ESP_LOGW(TAG, "RX buffer overflow, resetting stream buffer");
            s_rx_buf_len = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

esp_err_t cocis_mqtt_init(node_id_t my_node_id)
{
    s_my_node_id = my_node_id;
    snprintf(s_client_id, sizeof(s_client_id), "cocis_node_%u_%04lx",
             (unsigned)my_node_id, (unsigned long)(esp_random() & 0xFFFF));

    xTaskCreate(mqtt_task, "cocis_mqtt", 10240, NULL, 5, NULL);
    ESP_LOGI(TAG, "MQTT subsystem task spawned");
    return ESP_OK;
}

bool cocis_mqtt_is_connected(void)
{
    return s_connected;
}

esp_err_t cocis_mqtt_publish(const char *topic, const char *payload)
{
    if (s_sock < 0 || !s_connected || !topic || !payload) {
        return ESP_FAIL;
    }

    uint16_t tlen = (uint16_t)strlen(topic);
    size_t plen = strlen(payload);
    size_t rem_len = 2 + tlen + plen;

    uint8_t hdr[8];
    size_t hdr_len = 0;
    hdr[0] = 0x30; /* PUBLISH QoS 0 */

    if (rem_len < 128) {
        hdr[1] = (uint8_t)rem_len;
        hdr[2] = (uint8_t)(tlen >> 8);
        hdr[3] = (uint8_t)(tlen & 0xFF);
        hdr_len = 4;
    } else {
        hdr[1] = (uint8_t)((rem_len & 0x7F) | 0x80);
        hdr[2] = (uint8_t)(rem_len >> 7);
        hdr[3] = (uint8_t)(tlen >> 8);
        hdr[4] = (uint8_t)(tlen & 0xFF);
        hdr_len = 5;
    }

    if (cocis_sync_lock(COCIS_LOCK_LEVEL_NETWORK, CFG_LOCK_TIMEOUT_MS) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    if (mqtt_send_bytes(hdr, hdr_len) != ESP_OK ||
        mqtt_send_bytes((const uint8_t *)topic, tlen) != ESP_OK ||
        mqtt_send_bytes((const uint8_t *)payload, plen) != ESP_OK) {
        ret = ESP_FAIL;
    }

    cocis_sync_unlock(COCIS_LOCK_LEVEL_NETWORK);
    return ret;
}

esp_err_t cocis_mqtt_subscribe(const char *topic, cocis_mqtt_rx_cb_t callback)
{
    if (!topic || !callback || s_sub_count >= MAX_SUBSCRIPTIONS) {
        return ESP_ERR_NO_MEM;
    }

    strncpy(s_subs[s_sub_count].topic, topic, sizeof(s_subs[s_sub_count].topic) - 1);
    s_subs[s_sub_count].cb = callback;
    s_sub_count++;

    if (s_connected) {
        mqtt_subscribe_remote(topic);
    }
    return ESP_OK;
}
