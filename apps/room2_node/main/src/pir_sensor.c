#include "pir_sensor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "ROOM_PIR";

static gpio_num_t s_pir_pin = GPIO_NUM_27;
static pir_motion_cb_t s_callback = NULL;
static QueueHandle_t s_pir_queue = NULL;
static bool s_motion_state = false;

static void IRAM_ATTR pir_isr_handler(void *arg)
{
    (void)arg;
    int level = gpio_get_level(s_pir_pin);
    BaseType_t high_task_woken = pdFALSE;
    if (s_pir_queue != NULL) {
        xQueueSendFromISR(s_pir_queue, &level, &high_task_woken);
    }
    portYIELD_FROM_ISR(high_task_woken);
}

static void pir_task(void *arg)
{
    (void)arg;
    int level = 0;
    int64_t last_change_ms = 0;

    while (1) {
        if (xQueueReceive(s_pir_queue, &level, portMAX_DELAY) == pdTRUE) {
            int64_t now_ms = esp_timer_get_time() / 1000LL;
            if (now_ms - last_change_ms < 100) { /* 100ms debounce */
                continue;
            }
            last_change_ms = now_ms;
            bool new_motion = (level != 0);
            if (new_motion != s_motion_state) {
                s_motion_state = new_motion;
                ESP_LOGI(TAG, "PIR state changed -> %s (GPIO %d)",
                         s_motion_state ? "MOTION ACTIVE" : "MOTION CLEARED", (int)s_pir_pin);
                if (s_callback) {
                    s_callback(s_motion_state);
                }
            }
        }
    }
}

esp_err_t pir_sensor_init(gpio_num_t pin, pir_motion_cb_t cb)
{
    s_pir_pin = pin;
    s_callback = cb;

    if (s_pir_queue == NULL) {
        s_pir_queue = xQueueCreate(16, sizeof(int));
        if (s_pir_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    gpio_install_isr_service(0);
    gpio_isr_handler_add(pin, pir_isr_handler, NULL);

    s_motion_state = (gpio_get_level(pin) != 0);

    if (xTaskCreate(pir_task, "pir_task", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "PIR motion sensor task initialized on GPIO %d (initial state=%d)",
             (int)pin, (int)s_motion_state);
    return ESP_OK;
}

bool pir_sensor_is_motion_active(void)
{
    return s_motion_state;
}

void pir_sensor_simulate(bool active)
{
    int lvl = active ? 1 : 0;
    if (s_pir_queue != NULL) {
        xQueueSend(s_pir_queue, &lvl, pdMS_TO_TICKS(50));
    }
}
