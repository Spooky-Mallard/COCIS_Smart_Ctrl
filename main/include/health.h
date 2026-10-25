#ifndef HEALTH_H
#define HEALTH_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t uptime_sec;
    uint32_t free_heap_bytes;
    uint32_t min_free_heap_bytes;
    uint32_t health_task_stack_hwm;
    uint8_t  online_nodes;
    uint32_t watchdog_feeds;
    uint32_t node_timeout_alerts;
} health_summary_t;

esp_err_t health_init(void);
esp_err_t health_get_summary(health_summary_t *out_summary);

#endif
