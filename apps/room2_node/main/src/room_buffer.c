#include "room_buffer.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ROOM_BUFFER";

#define BUFFER_CAPACITY CFG_MAX_STORE_FORWARD_EVENTS

static proto_packet_t s_buffer[BUFFER_CAPACITY];
static size_t s_head = 0;
static size_t s_tail = 0;
static size_t s_count = 0;
static SemaphoreHandle_t s_mutex = NULL;

esp_err_t room_buffer_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }
    s_head = 0;
    s_tail = 0;
    s_count = 0;
    ESP_LOGI(TAG, "Store-and-forward FIFO ring buffer initialized (cap=%d)", BUFFER_CAPACITY);
    return ESP_OK;
}

esp_err_t room_buffer_push(const proto_packet_t *pkt)
{
    if (!pkt || !s_mutex) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_count >= BUFFER_CAPACITY) {
        /* Drop oldest entry to make room */
        s_head = (s_head + 1) % BUFFER_CAPACITY;
        s_count--;
        ESP_LOGW(TAG, "Ring buffer full! Dropped oldest entry to maintain FIFO order.");
    }

    s_buffer[s_tail] = *pkt;
    s_tail = (s_tail + 1) % BUFFER_CAPACITY;
    s_count++;

    ESP_LOGI(TAG, "Buffered packet seq=%lu (depth=%u/%u)",
             (unsigned long)pkt->seq_id, (unsigned)s_count, BUFFER_CAPACITY);

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

bool room_buffer_peek(proto_packet_t *out_pkt)
{
    if (!out_pkt || !s_mutex) {
        return false;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }

    if (s_count == 0) {
        xSemaphoreGive(s_mutex);
        return false;
    }

    *out_pkt = s_buffer[s_head];
    xSemaphoreGive(s_mutex);
    return true;
}

void room_buffer_pop(void)
{
    if (!s_mutex) {
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_count > 0) {
            s_head = (s_head + 1) % BUFFER_CAPACITY;
            s_count--;
        }
        xSemaphoreGive(s_mutex);
    }
}

size_t room_buffer_count(void)
{
    size_t count = 0;
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = s_count;
        xSemaphoreGive(s_mutex);
    }
    return count;
}

bool room_buffer_is_empty(void)
{
    return (room_buffer_count() == 0);
}
