#include "pir_sensor.h"
#include <stdio.h>
#include "esp_log.h"
#include "cocis_mqtt.h"
#include "cocis_config.h"

static const char *TAG = "ENTRANCE_PIR_PROXY";

esp_err_t pir_sensor_init(void)
{
    ESP_LOGI(TAG, "PIR sensor proxy initialized (Entrance Hub)");
    return ESP_OK;
}

esp_err_t pir_sensor_simulate_motion(uint8_t room_id, bool active)
{
    if (room_id < 1 || room_id > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/room/%u/control", CFG_MQTT_TOPIC_PREFIX, room_id);
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"cmd\":\"motion\",\"val\":%d}", active ? 1 : 0);

    ESP_LOGI(TAG, "Sent test motion simulation to Room %u (active=%d)", room_id, (int)active);
    return cocis_mqtt_publish(topic, payload);
}
