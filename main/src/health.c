#include "health.h"
#include <stdio.h>
#include "config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "logger.h"
#include "net_protocol.h"

static const char *TAG = "HEALTH";

static uint32_t     s_wdt_feeds = 0;
static uint32_t     s_node_alerts = 0;
static TaskHandle_t s_health_task_handle = NULL;
static bool         s_alerted_offline[NODE_ID_COUNT] = { false, false, false };

static void health_task(void *arg)
{
    (void)arg;
    bool wdt_registered = (esp_task_wdt_add(NULL) == ESP_OK);

    while (1) {
        if (wdt_registered) {
            esp_task_wdt_reset();
            s_wdt_feeds++;
        }

        int64_t now_s = esp_timer_get_time() / 1000000LL;
        for (int n = NODE_ID_ROOM_1; n <= NODE_ID_ROOM_2; n++) {
            node_net_status_t st;
            if (net_protocol_get_node_status((node_id_t)n, &st) == ESP_OK) {
                int64_t silent_sec = now_s - st.last_seen_uptime_sec;
                bool timed_out = (!st.link_simulated_online) || (silent_sec >= CFG_NODE_OFFLINE_THRESHOLD_SEC);
                if (timed_out && !s_alerted_offline[n]) {
                    s_alerted_offline[n] = true;
                    s_node_alerts++;
                    char detail[48];
                    snprintf(detail, sizeof(detail), "Node %d (%s) heartbeat lost", n, st.name);
                    logger_log_audit("HEALTH_MON", "NODE_OFFLINE_ALERT", detail);
                    ESP_LOGW(TAG, "%s", detail);
                } else if (!timed_out && s_alerted_offline[n]) {
                    s_alerted_offline[n] = false;
                    logger_log_audit("HEALTH_MON", "NODE_RECOVERED", st.name);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t health_init(void)
{
    if (xTaskCreate(health_task, "health_task", 4096, NULL, 2, &s_health_task_handle) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Task watchdog and distributed node heartbeat monitor started");
    return ESP_OK;
}

esp_err_t health_get_summary(health_summary_t *out_summary)
{
    if (out_summary == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t now_s = esp_timer_get_time() / 1000000LL;
    uint8_t online = 1; /* Entrance node */
    for (int n = NODE_ID_ROOM_1; n <= NODE_ID_ROOM_2; n++) {
        node_net_status_t st;
        if (net_protocol_get_node_status((node_id_t)n, &st) == ESP_OK) {
            if (st.link_simulated_online && (now_s - st.last_seen_uptime_sec) < CFG_NODE_OFFLINE_THRESHOLD_SEC) {
                online++;
            }
        }
    }

    out_summary->uptime_sec = (uint32_t)now_s;
    out_summary->free_heap_bytes = esp_get_free_heap_size();
    out_summary->min_free_heap_bytes = esp_get_minimum_free_heap_size();
    out_summary->health_task_stack_hwm = (s_health_task_handle != NULL)
                                         ? (uint32_t)uxTaskGetStackHighWaterMark(s_health_task_handle) : 0;
    out_summary->online_nodes = online;
    out_summary->watchdog_feeds = s_wdt_feeds;
    out_summary->node_timeout_alerts = s_node_alerts;
    return ESP_OK;
}
