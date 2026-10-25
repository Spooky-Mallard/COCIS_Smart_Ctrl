#include "cocis_sync.h"
#include "esp_log.h"

static const char *TAG = "cocis_sync";

static SemaphoreHandle_t s_mutexes[COCIS_LOCK_LEVEL_COUNT] = { NULL };
static EventGroupHandle_t s_event_group = NULL;
static bool s_initialized = false;

esp_err_t cocis_sync_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    for (int i = 0; i < COCIS_LOCK_LEVEL_COUNT; i++) {
        s_mutexes[i] = xSemaphoreCreateMutex();
        if (!s_mutexes[i]) {
            ESP_LOGE(TAG, "Failed to create mutex level %d", i);
            return ESP_ERR_NO_MEM;
        }
    }

    s_event_group = xEventGroupCreate();
    if (!s_event_group) {
        ESP_LOGE(TAG, "Failed to create system event group");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Hierarchical mutexes and event groups initialized");
    return ESP_OK;
}

esp_err_t cocis_sync_lock(cocis_lock_level_t level, uint32_t timeout_ms)
{
    if (!s_initialized || level >= COCIS_LOCK_LEVEL_COUNT || !s_mutexes[level]) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_mutexes[level], ticks) != pdTRUE) {
        ESP_LOGW(TAG, "Lock level %d acquisition timed out (%lu ms)", (int)level, (unsigned long)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void cocis_sync_unlock(cocis_lock_level_t level)
{
    if (s_initialized && level < COCIS_LOCK_LEVEL_COUNT && s_mutexes[level]) {
        xSemaphoreGive(s_mutexes[level]);
    }
}

void cocis_sync_event_set(uint32_t bits)
{
    if (s_initialized && s_event_group) {
        xEventGroupSetBits(s_event_group, (EventBits_t)bits);
    }
}

void cocis_sync_event_clear(uint32_t bits)
{
    if (s_initialized && s_event_group) {
        xEventGroupClearBits(s_event_group, (EventBits_t)bits);
    }
}

bool cocis_sync_event_check(uint32_t bits)
{
    if (!s_initialized || !s_event_group) {
        return false;
    }
    EventBits_t current = xEventGroupGetBits(s_event_group);
    return ((current & (EventBits_t)bits) == (EventBits_t)bits);
}

uint32_t cocis_sync_event_wait(uint32_t bits, bool clear_on_exit, bool wait_for_all, uint32_t timeout_ms)
{
    if (!s_initialized || !s_event_group) {
        return 0;
    }
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (uint32_t)xEventGroupWaitBits(s_event_group, (EventBits_t)bits,
                                         clear_on_exit ? pdTRUE : pdFALSE,
                                         wait_for_all ? pdTRUE : pdFALSE,
                                         ticks);
}
