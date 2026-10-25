#include "buzzer.h"
#include "board_pins.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "BUZZER";

static QueueHandle_t s_buzzer_queue = NULL;

static void tone_burst(uint32_t freq_hz, uint32_t duration_ms)
{
    if (freq_hz > 0) {
        ledc_set_freq(BOARD_LEDC_SPEED_MODE, BOARD_LEDC_BUZZER_TIMER, freq_hz);
        ledc_set_duty(BOARD_LEDC_SPEED_MODE, BOARD_LEDC_CH_BUZZER, 512);
        ledc_update_duty(BOARD_LEDC_SPEED_MODE, BOARD_LEDC_CH_BUZZER);
    }
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ledc_set_duty(BOARD_LEDC_SPEED_MODE, BOARD_LEDC_CH_BUZZER, 0);
    ledc_update_duty(BOARD_LEDC_SPEED_MODE, BOARD_LEDC_CH_BUZZER);
}

static void buzzer_task(void *arg)
{
    (void)arg;
    buzzer_pattern_t pat;
    while (1) {
        if (xQueueReceive(s_buzzer_queue, &pat, portMAX_DELAY) == pdTRUE) {
            switch (pat) {
                case BUZZER_PAT_GRANT:
                    tone_burst(2200, 90);
                    break;
                case BUZZER_PAT_DENY:
                    tone_burst(450, 120);
                    vTaskDelay(pdMS_TO_TICKS(60));
                    tone_burst(380, 160);
                    break;
                case BUZZER_PAT_ANTIPASSBACK:
                    for (int i = 0; i < 3; i++) {
                        tone_burst(900, 70);
                        vTaskDelay(pdMS_TO_TICKS(40));
                    }
                    break;
                case BUZZER_PAT_DOOR_ALARM:
                    for (int i = 0; i < 4; i++) {
                        tone_burst(1800, 100);
                        tone_burst(800, 100);
                    }
                    break;
                case BUZZER_PAT_EMERGENCY:
                    for (int i = 0; i < 3; i++) {
                        tone_burst(2600, 140);
                        tone_burst(1200, 140);
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

esp_err_t buzzer_init(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode = BOARD_LEDC_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BOARD_LEDC_BUZZER_TIMER,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        return err;
    }

    ledc_channel_config_t ccfg = {
        .gpio_num = PIN_BUZZER,
        .speed_mode = BOARD_LEDC_SPEED_MODE,
        .channel = BOARD_LEDC_CH_BUZZER,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BOARD_LEDC_BUZZER_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    err = ledc_channel_config(&ccfg);
    if (err != ESP_OK) {
        return err;
    }

    if (s_buzzer_queue == NULL) {
        s_buzzer_queue = xQueueCreate(8, sizeof(buzzer_pattern_t));
        if (s_buzzer_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (xTaskCreate(buzzer_task, "buzzer_task", 3072, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LEDC piezo buzzer pattern engine initialized on GPIO %d", (int)PIN_BUZZER);
    return ESP_OK;
}

esp_err_t buzzer_play(buzzer_pattern_t pattern)
{
    if (s_buzzer_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xQueueSend(s_buzzer_queue, &pattern, 0);
    return ESP_OK;
}
