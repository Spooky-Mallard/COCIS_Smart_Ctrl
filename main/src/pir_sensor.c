#include "pir_sensor.h"
#include "board_pins.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "light.h"

static const char *TAG = "PIR_SENSOR";

typedef struct {
    uint8_t room_id;
    bool    level;
} pir_edge_t;

static QueueHandle_t s_pir_queue = NULL;
static bool          s_sim_override[CFG_ROOM_COUNT] = { false, false };
static bool          s_last_state[CFG_ROOM_COUNT] = { false, false };

static void IRAM_ATTR pir_gpio_isr(void *arg)
{
    uint8_t room_id = (uint8_t)(uintptr_t)arg;
    gpio_num_t pin = (room_id == 1) ? PIN_PIR_ROOM1 : PIN_PIR_ROOM2;
    pir_edge_t evt = {
        .room_id = room_id,
        .level = (gpio_get_level(pin) != 0)
    };
    BaseType_t high_woken = pdFALSE;
    if (s_pir_queue != NULL) {
        xQueueSendFromISR(s_pir_queue, &evt, &high_woken);
    }
    portYIELD_FROM_ISR(high_woken);
}

static void pir_light_task(void *arg)
{
    (void)arg;
    int64_t last_tick_ms = esp_timer_get_time() / 1000LL;
    int64_t last_edge_ms[CFG_ROOM_COUNT] = { 0, 0 };
    pir_edge_t evt;

    while (1) {
        if (xQueueReceive(s_pir_queue, &evt, pdMS_TO_TICKS(200)) == pdTRUE) {
            if (evt.room_id >= 1 && evt.room_id <= CFG_ROOM_COUNT) {
                uint8_t idx = (uint8_t)(evt.room_id - 1);
                int64_t now_ms = esp_timer_get_time() / 1000LL;
                if ((now_ms - last_edge_ms[idx]) >= CFG_PIR_DEBOUNCE_MS) {
                    last_edge_ms[idx] = now_ms;
                    bool effective = evt.level || s_sim_override[idx];
                    if (effective != s_last_state[idx]) {
                        s_last_state[idx] = effective;
                        light_notify_motion(evt.room_id, effective);
                    }
                }
            }
        }

        int64_t now_ms = esp_timer_get_time() / 1000LL;
        if ((now_ms - last_tick_ms) >= 1000) {
            last_tick_ms = now_ms;
            light_tick_1hz();
        }
    }
}

esp_err_t pir_sensor_init(void)
{
    if (s_pir_queue == NULL) {
        s_pir_queue = xQueueCreate(16, sizeof(pir_edge_t));
        if (s_pir_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    gpio_config_t pcfg = {
        .pin_bit_mask = (1ULL << PIN_PIR_ROOM1) | (1ULL << PIN_PIR_ROOM2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    esp_err_t err = gpio_config(&pcfg);
    if (err != ESP_OK) {
        return err;
    }

    gpio_isr_handler_add(PIN_PIR_ROOM1, pir_gpio_isr, (void *)(uintptr_t)1);
    gpio_isr_handler_add(PIN_PIR_ROOM2, pir_gpio_isr, (void *)(uintptr_t)2);

    if (xTaskCreate(pir_light_task, "pir_light_task", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "PIR motion sensors initialized (Room 1=GPIO %d, Room 2=GPIO %d)",
             (int)PIN_PIR_ROOM1, (int)PIN_PIR_ROOM2);
    return ESP_OK;
}

esp_err_t pir_sensor_simulate_motion(uint8_t room_id, bool motion_active)
{
    if (room_id < 1 || room_id > CFG_ROOM_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t idx = (uint8_t)(room_id - 1);
    s_sim_override[idx] = motion_active;
    s_last_state[idx] = motion_active;
    return light_notify_motion(room_id, motion_active);
}

bool pir_sensor_get_state(uint8_t room_id)
{
    if (room_id < 1 || room_id > CFG_ROOM_COUNT) {
        return false;
    }
    return s_last_state[room_id - 1];
}
