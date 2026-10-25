#ifndef HEALTH_H
#define HEALTH_H

#include "cocis_health.h"
#include "esp_system.h"

typedef struct {
    uint32_t online_nodes;
    uint32_t free_heap_bytes;
    uint32_t uptime_sec;
} health_summary_t;

static inline esp_err_t health_init(void) {
    return cocis_health_init(NODE_ID_ENTRANCE);
}

static inline bool health_is_healthy(void) {
    return true;
}

static inline void health_report_alive(void) {
}

static inline esp_err_t health_get_summary(health_summary_t *out_sum) {
    if (out_sum) {
        uint32_t online = 1;
        if (cocis_health_is_node_alive(NODE_ID_ROOM_1)) online++;
        if (cocis_health_is_node_alive(NODE_ID_ROOM_2)) online++;
        out_sum->online_nodes = online;
        out_sum->free_heap_bytes = (uint32_t)esp_get_free_heap_size();
        out_sum->uptime_sec = cocis_uptime_sec();
    }
    return ESP_OK;
}

#endif /* HEALTH_H */
