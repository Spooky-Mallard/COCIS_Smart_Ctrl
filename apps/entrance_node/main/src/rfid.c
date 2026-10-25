#include "rfid.h"
#include <ctype.h>
#include <string.h>
#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "RFID";

static QueueHandle_t              s_btn_isr_queue = NULL;
static rfid_scan_callback_t       s_scan_cb = NULL;
static rfid_emergency_button_cb_t s_emerg_cb = NULL;

static void IRAM_ATTR button_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    BaseType_t high_task_woken = pdFALSE;
    if (s_btn_isr_queue != NULL) {
        xQueueSendFromISR(s_btn_isr_queue, &gpio_num, &high_task_woken);
    }
    portYIELD_FROM_ISR(high_task_woken);
}

static void rfid_task(void *arg)
{
    (void)arg;
    int64_t last_press_ms[40] = { 0 };
    uint32_t gpio_num = 0;

    while (1) {
        if (xQueueReceive(s_btn_isr_queue, &gpio_num, portMAX_DELAY) == pdTRUE) {
            if (gpio_num >= 40) {
                continue;
            }
            int64_t now_ms = esp_timer_get_time() / 1000LL;
            if ((now_ms - last_press_ms[gpio_num]) < CFG_RFID_DEBOUNCE_MS) {
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            if (gpio_get_level((gpio_num_t)gpio_num) != 0) {
                continue;
            }
            last_press_ms[gpio_num] = esp_timer_get_time() / 1000LL;

            if (gpio_num == PIN_BTN_EMERGENCY) {
                if (s_emerg_cb != NULL) {
                    s_emerg_cb();
                }
                continue;
            }

            rfid_scan_event_t evt = { 0 };
            evt.from_hardware_btn = true;
            if (gpio_num == PIN_BTN_ENTRY_SCAN) {
                evt.reader = RFID_READER_ENTRY;
                strncpy(evt.uid, CFG_BTN_ENTRY_VALID_UID, sizeof(evt.uid) - 1);
            } else if (gpio_num == PIN_BTN_EXIT_SCAN) {
                evt.reader = RFID_READER_EXIT;
                strncpy(evt.uid, CFG_BTN_EXIT_VALID_UID, sizeof(evt.uid) - 1);
            } else if (gpio_num == PIN_BTN_INTRUDER_SCAN) {
                evt.reader = RFID_READER_ENTRY;
                strncpy(evt.uid, CFG_BTN_INTRUDER_UID, sizeof(evt.uid) - 1);
            } else {
                continue;
            }

            if (s_scan_cb != NULL) {
                s_scan_cb(&evt);
            }
        }
    }
}

esp_err_t rfid_init(rfid_scan_callback_t scan_cb, rfid_emergency_button_cb_t emerg_cb)
{
    s_scan_cb = scan_cb;
    s_emerg_cb = emerg_cb;

    if (s_btn_isr_queue == NULL) {
        s_btn_isr_queue = xQueueCreate(CFG_RFID_QUEUE_DEPTH, sizeof(uint32_t));
        if (s_btn_isr_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    uint64_t btn_mask = (1ULL << PIN_BTN_ENTRY_SCAN)    |
                        (1ULL << PIN_BTN_EXIT_SCAN)     |
                        (1ULL << PIN_BTN_INTRUDER_SCAN) |
                        (1ULL << PIN_BTN_EMERGENCY);

    gpio_config_t bcfg = {
        .pin_bit_mask = btn_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    esp_err_t err = gpio_config(&bcfg);
    if (err != ESP_OK) {
        return err;
    }

    const gpio_num_t btns[4] = {
        PIN_BTN_ENTRY_SCAN,
        PIN_BTN_EXIT_SCAN,
        PIN_BTN_INTRUDER_SCAN,
        PIN_BTN_EMERGENCY
    };
    for (int i = 0; i < 4; i++) {
        gpio_isr_handler_add(btns[i], button_isr_handler, (void *)(uintptr_t)btns[i]);
    }

    if (xTaskCreate(rfid_task, "rfid_task", 4096, NULL, 7, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Entry/Exit RFID reader abstraction and button ISRs initialized");
    return ESP_OK;
}

esp_err_t rfid_inject_scan(rfid_reader_t reader, const char *uid)
{
    if (uid == NULL || uid[0] == '\0' || (reader != RFID_READER_ENTRY && reader != RFID_READER_EXIT)) {
        return ESP_ERR_INVALID_ARG;
    }

    rfid_scan_event_t evt = { 0 };
    evt.reader = reader;
    evt.from_hardware_btn = false;

    size_t i = 0;
    while (uid[i] != '\0' && i + 1 < sizeof(evt.uid)) {
        evt.uid[i] = (char)toupper((unsigned char)uid[i]);
        i++;
    }
    evt.uid[i] = '\0';

    if (s_scan_cb != NULL) {
        s_scan_cb(&evt);
    }
    return ESP_OK;
}
