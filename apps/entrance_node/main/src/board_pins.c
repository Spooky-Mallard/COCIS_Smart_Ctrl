#include "board_pins.h"
#include "esp_log.h"

static const char *TAG = "BOARD_PINS";

esp_err_t board_pins_init(void)
{
    uint64_t out_mask = (1ULL << PIN_LED_ENTRY) |
                        (1ULL << PIN_LED_EXIT)  |
                        (1ULL << PIN_LED_ALARM);

    gpio_config_t out_cfg = {
        .pin_bit_mask = out_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&out_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure output pins: %s", esp_err_to_name(err));
        return err;
    }

    gpio_set_level(PIN_LED_ENTRY, 0);
    gpio_set_level(PIN_LED_EXIT, 0);
    gpio_set_level(PIN_LED_ALARM, 0);

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Entrance Hub GPIO outputs and ISR service initialized");
    return ESP_OK;
}
