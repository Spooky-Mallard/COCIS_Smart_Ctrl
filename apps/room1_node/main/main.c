#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cocis_types.h"
#include "cocis_config.h"
#include "cocis_crypto.h"
#include "cocis_sync.h"
#include "cocis_time.h"
#include "cocis_wifi.h"
#include "cocis_mqtt.h"
#include "cocis_health.h"
#include "cocis_protocol.h"

#include "pir_sensor.h"
#include "light.h"
#include "room_buffer.h"
#include "room_cli.h"

#define PIN_PIR        GPIO_NUM_27
#define PIN_RELAY      GPIO_NUM_12
#define PIN_STATUS_LED GPIO_NUM_2
#define MY_NODE_ID     NODE_ID_ROOM_1
#define MY_ROOM_ID     1

static const char *TAG = "ROOM1_MAIN";
static uint32_t s_pending_ack_seq = 0;

static void send_or_buffer_packet(proto_packet_t *pkt)
{
    pkt->version = 1;
    pkt->src_node = MY_NODE_ID;
    pkt->dst_node = NODE_ID_ENTRANCE;
    pkt->seq_id = cocis_proto_next_seq(MY_NODE_ID);
    pkt->timestamp_epoch = cocis_time_now_epoch();
    pkt->room_id = MY_ROOM_ID;
    pkt->uptime_sec = cocis_uptime_sec();

    char buf[256];
    if (cocis_proto_serialize(pkt, CFG_SECRET_KEY, buf, sizeof(buf)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to serialize packet with HMAC");
        return;
    }

    if (cocis_mqtt_is_connected()) {
        char topic[64];
        snprintf(topic, sizeof(topic), "%s/room/%u/telemetry", CFG_MQTT_TOPIC_PREFIX, MY_ROOM_ID);
        if (cocis_mqtt_publish(topic, buf) == ESP_OK) {
            s_pending_ack_seq = pkt->seq_id;
            ESP_LOGI(TAG, "Published live event seq=%lu to Hub via MQTT", (unsigned long)pkt->seq_id);
            return;
        }
    }

    /* If offline or publish failed -> store and forward */
    room_buffer_push(pkt);
}

static void on_light_state_change(light_state_t new_state, bool occupied, uint32_t on_sec, uint32_t saved_mwh)
{
    proto_packet_t pkt = {
        .msg_type = PROTO_MSG_ROOM_EVENT,
        .occupied = occupied ? 1 : 0,
        .light_state = (uint8_t)new_state,
        .light_on_sec = on_sec,
        .energy_saved_mwh = saved_mwh
    };
    send_or_buffer_packet(&pkt);
}

static void on_pir_motion(bool motion_active)
{
    light_handle_motion(motion_active);
}

static void on_hub_control_message(const char *topic, const char *payload)
{
    (void)topic;
    proto_packet_t pkt;
    esp_err_t err = cocis_proto_deserialize(payload, CFG_SECRET_KEY, &pkt);
    if (err != ESP_OK) {
        /* Check if it was a plain JSON command like test motion */
        if (strstr(payload, "\"cmd\":\"motion\"")) {
            int val = 0;
            if (sscanf(payload, "{\"cmd\":\"motion\",\"val\":%d}", &val) == 1) {
                pir_sensor_simulate(val != 0);
            }
        }
        return;
    }

    if (pkt.msg_type == PROTO_MSG_ACK) {
        if (pkt.ack_seq_id == s_pending_ack_seq) {
            s_pending_ack_seq = 0;
            ESP_LOGI(TAG, "Received ACK from Hub for live packet seq=%lu", (unsigned long)pkt.ack_seq_id);
        }

        proto_packet_t front;
        if (room_buffer_peek(&front) && front.seq_id == pkt.ack_seq_id) {
            room_buffer_pop();
            ESP_LOGI(TAG, "Replayed packet seq=%lu acknowledged and popped from buffer", (unsigned long)pkt.ack_seq_id);
        }
    } else if (pkt.msg_type == PROTO_MSG_LIGHT_OVERRIDE) {
        ESP_LOGW(TAG, "Received remote light override command from Entrance Hub: %u", (unsigned)pkt.light_state);
        light_set_override(pkt.light_state);
    } else if (pkt.msg_type == PROTO_MSG_EMERGENCY) {
        if (pkt.emergency_active) {
            ESP_LOGE(TAG, "EMERGENCY EVACUATION ACTIVE! Turning lights ON immediately for physical safety.");
            light_set_override(1);
        } else {
            ESP_LOGI(TAG, "Emergency cleared. Resuming AUTO lighting.");
            light_set_override(2);
        }
    }
}

void app_main(void)
{
    printf("\n");
    printf("====================================================================\n");
    printf("     CoCIS SMART CONTROL SYSTEM - LECTURE ROOM 1 TERMINAL           \n");
    printf("     Makerere University | BSE III & CS II Coursework Project 2026  \n");
    printf("     Role: NODE_ID_ROOM_1 | PIR: GPIO 27 | Relay: GPIO 12           \n");
    printf("====================================================================\n\n");

    /* 1. System Synchronization, NVS & Cryptography */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    cocis_sync_init();
    cocis_crypto_init();
    cocis_time_init();
    cocis_health_init(MY_NODE_ID);
    cocis_proto_init();
    room_buffer_init();

    /* 2. Configure Status LED */
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << PIN_STATUS_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);

    /* 3. Peripherals */
    light_init(PIN_RELAY, on_light_state_change);
    pir_sensor_init(PIN_PIR, on_pir_motion);

    /* 4. Networking & Remote Subscriptions */
    cocis_wifi_init();
    cocis_mqtt_init(MY_NODE_ID);

    char ctrl_topic[64];
    snprintf(ctrl_topic, sizeof(ctrl_topic), "%s/room/%u/control", CFG_MQTT_TOPIC_PREFIX, MY_ROOM_ID);
    cocis_mqtt_subscribe(ctrl_topic, on_hub_control_message);

    char bcast_topic[64];
    snprintf(bcast_topic, sizeof(bcast_topic), "%s/broadcast", CFG_MQTT_TOPIC_PREFIX);
    cocis_mqtt_subscribe(bcast_topic, on_hub_control_message);

    /* 5. Diagnostic Console */
    room_cli_init();

    ESP_LOGI(TAG, "Room 1 terminal running autonomously. Store-and-forward active.");

    /* 6. Main Autonomous Loop */
    uint32_t ticks = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ticks++;

        light_tick_1hz();

        /* Blink Status LED */
        gpio_set_level(PIN_STATUS_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(PIN_STATUS_LED, 0);

        /* Send Heartbeat every 5 seconds */
        if (ticks % CFG_HEARTBEAT_INTERVAL_SEC == 0 && cocis_mqtt_is_connected()) {
            proto_packet_t hb = {
                .version = 1,
                .msg_type = PROTO_MSG_HEARTBEAT,
                .src_node = MY_NODE_ID,
                .dst_node = NODE_ID_ENTRANCE,
                .seq_id = cocis_proto_next_seq(MY_NODE_ID),
                .timestamp_epoch = cocis_time_now_epoch(),
                .room_id = MY_ROOM_ID,
                .occupied = pir_sensor_is_motion_active() ? 1 : 0,
                .light_state = (uint8_t)light_get_state(),
                .light_on_sec = light_get_on_seconds(),
                .energy_saved_mwh = light_get_energy_saved_mwh(),
                .uptime_sec = cocis_uptime_sec()
            };

            char hb_buf[256];
            if (cocis_proto_serialize(&hb, CFG_SECRET_KEY, hb_buf, sizeof(hb_buf)) == ESP_OK) {
                char telem_topic[64];
                snprintf(telem_topic, sizeof(telem_topic), "%s/room/%u/telemetry", CFG_MQTT_TOPIC_PREFIX, MY_ROOM_ID);
                cocis_mqtt_publish(telem_topic, hb_buf);
            }
        }

        /* Drain Store-and-Forward Buffer if online */
        if (cocis_mqtt_is_connected() && !room_buffer_is_empty()) {
            proto_packet_t front;
            if (room_buffer_peek(&front)) {
                char replay_buf[256];
                if (cocis_proto_serialize(&front, CFG_SECRET_KEY, replay_buf, sizeof(replay_buf)) == ESP_OK) {
                    char telem_topic[64];
                    snprintf(telem_topic, sizeof(telem_topic), "%s/room/%u/telemetry", CFG_MQTT_TOPIC_PREFIX, MY_ROOM_ID);
                    cocis_mqtt_publish(telem_topic, replay_buf);
                    ESP_LOGI(TAG, "Flushed buffered packet seq=%lu to Hub", (unsigned long)front.seq_id);
                }
            }
        }
    }
}
