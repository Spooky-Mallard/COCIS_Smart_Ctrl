#include "sync.h"
#include "esp_log.h"

static const char *TAG = "SYNC";

static SemaphoreHandle_t s_mutexes[LOCK_LEVEL_COUNT] = { NULL };
static EventGroupHandle_t s_sys_events = NULL;
static uint32_t s_acquire_counts[LOCK_LEVEL_COUNT] = { 0 };
static uint32_t s_timeout_counts[LOCK_LEVEL_COUNT] = { 0 };

static const char *const s_lock_names[LOCK_LEVEL_COUNT] = {
    "NONE",
    "L1_CONFIG",
    "L2_USER_DB",
    "L3_SCHEDULE",
    "L4_LEDGER",
    "L5_ROOM",
    "L6_DISPLAY",
    "L7_SD"
};

esp_err_t sync_init(void)
{
    if (s_sys_events == NULL) {
        s_sys_events = xEventGroupCreate();
        if (s_sys_events == NULL) {
            ESP_LOGE(TAG, "Failed to create system event group");
            return ESP_ERR_NO_MEM;
        }
    }

    for (int i = LOCK_LEVEL_CONFIG; i < LOCK_LEVEL_COUNT; i++) {
        if (s_mutexes[i] == NULL) {
            s_mutexes[i] = xSemaphoreCreateMutex();
            if (s_mutexes[i] == NULL) {
                ESP_LOGE(TAG, "Failed to create mutex for %s", s_lock_names[i]);
                return ESP_ERR_NO_MEM;
            }
        }
    }

    ESP_LOGI(TAG, "Initialized %d hierarchical mutexes and event group", LOCK_LEVEL_COUNT - 1);
    return ESP_OK;
}

esp_err_t sync_lock(sync_lock_level_t level, uint32_t timeout_ms)
{
    if (level <= LOCK_LEVEL_NONE || level >= LOCK_LEVEL_COUNT || s_mutexes[level] == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_mutexes[level], ticks) != pdTRUE) {
        s_timeout_counts[level]++;
        ESP_LOGW(TAG, "Lock timeout on %s after %lu ms", s_lock_names[level], (unsigned long)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }

    s_acquire_counts[level]++;
    return ESP_OK;
}

esp_err_t sync_unlock(sync_lock_level_t level)
{
    if (level <= LOCK_LEVEL_NONE || level >= LOCK_LEVEL_COUNT || s_mutexes[level] == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreGive(s_mutexes[level]) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

void sync_event_set(EventBits_t bits)
{
    if (s_sys_events != NULL) {
        xEventGroupSetBits(s_sys_events, bits);
    }
}

void sync_event_clear(EventBits_t bits)
{
    if (s_sys_events != NULL) {
        xEventGroupClearBits(s_sys_events, bits);
    }
}

bool sync_event_is_set(EventBits_t bit)
{
    if (s_sys_events == NULL) {
        return false;
    }
    return (xEventGroupGetBits(s_sys_events) & bit) != 0;
}

EventBits_t sync_event_get_all(void)
{
    if (s_sys_events == NULL) {
        return 0;
    }
    return xEventGroupGetBits(s_sys_events);
}

esp_err_t sync_get_stats(sync_lock_level_t level, sync_lock_stats_t *out_stats)
{
    if (level <= LOCK_LEVEL_NONE || level >= LOCK_LEVEL_COUNT || out_stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out_stats->name = s_lock_names[level];
    out_stats->acquire_count = s_acquire_counts[level];
    out_stats->timeout_count = s_timeout_counts[level];
    return ESP_OK;
}
