#include "cocis_health.h"
#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cocis_config.h"
#include "cocis_sync.h"

static const char *TAG = "cocis_health";

static node_id_t s_my_id = NODE_ID_ENTRANCE;
static node_net_status_t s_nodes[NODE_ID_COUNT];
static bool s_initialized = false;

uint32_t cocis_uptime_sec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

static void health_monitor_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t now = cocis_uptime_sec();

        for (int i = 0; i < NODE_ID_COUNT; i++) {
            if ((node_id_t)i == s_my_id) {
                s_nodes[i].last_seen_uptime_sec = now;
                s_nodes[i].heartbeat_healthy = true;
                continue;
            }

            if (s_nodes[i].last_seen_uptime_sec > 0 &&
                (now - (uint32_t)s_nodes[i].last_seen_uptime_sec) > CFG_NODE_OFFLINE_TIMEOUT_SEC) {
                if (s_nodes[i].heartbeat_healthy) {
                    s_nodes[i].heartbeat_healthy = false;
                    ESP_LOGW(TAG, "ALERT: Node %d missed 3 heartbeats! Marked UNREACHABLE.", i);
                }
            }
        }
    }
}

esp_err_t cocis_health_init(node_id_t my_node_id)
{
    if (s_initialized) {
        return ESP_OK;
    }

    s_my_id = my_node_id;
    memset(s_nodes, 0, sizeof(s_nodes));

    for (int i = 0; i < NODE_ID_COUNT; i++) {
        s_nodes[i].last_seen_uptime_sec = 0;
        s_nodes[i].heartbeat_healthy = ((node_id_t)i == my_node_id);
        s_nodes[i].rssi = -127;
    }

    s_initialized = true;
    xTaskCreate(health_monitor_task, "health_mon", 2048, NULL, 4, NULL);
    ESP_LOGI(TAG, "Health monitor initialized for Node %d", (int)my_node_id);
    return ESP_OK;
}

void cocis_health_record_rx(node_id_t src_node, uint32_t seq_id, uint32_t uptime_sec, int8_t rssi)
{
    if (src_node >= NODE_ID_COUNT) {
        return;
    }

    s_nodes[src_node].last_seen_uptime_sec = cocis_uptime_sec();
    s_nodes[src_node].heartbeat_healthy = true;
    s_nodes[src_node].rssi = rssi;
    if (seq_id > s_nodes[src_node].last_rx_seq) {
        s_nodes[src_node].last_rx_seq = seq_id;
    }
    s_nodes[src_node].acked_count++;
}

bool cocis_health_is_node_alive(node_id_t node_id)
{
    if (node_id >= NODE_ID_COUNT) {
        return false;
    }
    return s_nodes[node_id].heartbeat_healthy;
}

const node_net_status_t *cocis_health_get_status(node_id_t node_id)
{
    if (node_id >= NODE_ID_COUNT) {
        return NULL;
    }
    return &s_nodes[node_id];
}
