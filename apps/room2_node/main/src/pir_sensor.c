#include "pir_sensor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ROOM_PIR";

static gpio_num_t s_pir_pin = GPIO_NUM_27;
static pir_motion_cb_t s_callback = NULL;
static bool s_motion_state = false;
static int64_t s_last_change_ms = 0;

static void IRAM_ATTR pir_isr_handler(void *arg)
{
    (void)arg;
    int64_t now_ms = esp_timer_get_time() / 1000LL;
    if (now_ms - s_last_change_ms > 100) { /* 100ms debounce */
        s_last_change_ms = now_ms;
        int level = gpio_get_level(s_pir_pin);
        s_motion_state = (level != 0);
        if (s_callback) {
            s_callback(s_motion_state);
        }
    }
}

esp_err_t pir_sensor_init(gpio_num_t pin, pir_motion_cb_t cb)
{
    s_pir_pin = pin;
    s_callback = cb;

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
    ESP_LOGI(TAG, "PIR motion sensor initialized on GPIO %d (initial state=%d)",
             (int)pin, (int)s_motion_state);
    return ESP_OK;
}

bool pir_sensor_is_motion_active(void)
{
    return s_motion_state;
}

void pir_sensor_simulate(bool active)
{
    s_motion_state = active;
    ESP_LOGI(TAG, "Simulated motion state injected: %s", active ? "ACTIVE" : "IDLE");
    if (s_callback) {
        s_callback(active);
    }
}
