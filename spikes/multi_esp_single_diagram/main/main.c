#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define PIN_STRAP_BIT1     GPIO_NUM_34
#define PIN_STRAP_BIT0     GPIO_NUM_35
#define PIN_LED            GPIO_NUM_2

#define PIN_INTERCONNECT_1 GPIO_NUM_18
#define PIN_INTERCONNECT_2 GPIO_NUM_19

typedef enum {
    ROLE_ENTRANCE = 0, /* 00 */
    ROLE_ROOM_1   = 1, /* 01 */
    ROLE_ROOM_2   = 2, /* 10 */
    ROLE_UNKNOWN  = 3  /* 11 */
} node_role_t;

static const char *role_to_str(node_role_t role)
{
    switch (role) {
    case ROLE_ENTRANCE: return "NODE_ENTRANCE (00)";
    case ROLE_ROOM_1:   return "NODE_ROOM_1 (01)";
    case ROLE_ROOM_2:   return "NODE_ROOM_2 (10)";
    default:            return "ROLE_UNKNOWN (11)";
    }
}

void app_main(void)
{
    /* 1. Configure strapping pins (GPIO 34, 35) as input-only pins (external pull in diagram) */
    gpio_config_t strap_conf = {
        .pin_bit_mask = (1ULL << PIN_STRAP_BIT1) | (1ULL << PIN_STRAP_BIT0),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&strap_conf);

    /* 2. Configure diagnostic LED (GPIO 2) */
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << PIN_LED),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);

    /* Allow strapping line voltages to stabilize */
    vTaskDelay(pdMS_TO_TICKS(150));

    int bit1 = gpio_get_level(PIN_STRAP_BIT1);
    int bit0 = gpio_get_level(PIN_STRAP_BIT0);
    node_role_t role = (node_role_t)((bit1 << 1) | bit0);

    /* 3. Configure Interconnect Pins based on role */
    if (role == ROLE_ENTRANCE) {
        /* Entrance listens for pulses from Room 1 (GPIO 18) and Room 2 (GPIO 19) */
        gpio_config_t in_conf = {
            .pin_bit_mask = (1ULL << PIN_INTERCONNECT_1) | (1ULL << PIN_INTERCONNECT_2),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&in_conf);
    } else if (role == ROLE_ROOM_1) {
        /* Room 1 pulses GPIO 18 */
        gpio_config_t out_conf = {
            .pin_bit_mask = (1ULL << PIN_INTERCONNECT_1),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&out_conf);
        gpio_set_level(PIN_INTERCONNECT_1, 0);
    } else if (role == ROLE_ROOM_2) {
        /* Room 2 pulses GPIO 19 */
        gpio_config_t out_conf = {
            .pin_bit_mask = (1ULL << PIN_INTERCONNECT_2),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&out_conf);
        gpio_set_level(PIN_INTERCONNECT_2, 0);
    }

    /* 4. Print Boot Banner */
    printf("\n");
    printf("============================================================\n");
    printf(" [WOKWI MULTI-ESP TEST] ESP32 BOOT INITIALIZED\n");
    printf(" Strapping Pins Read: GPIO 34 = %d, GPIO 35 = %d\n", bit1, bit0);
    printf(" Derived Node Role:   0x%02X -> %s\n", (unsigned)role, role_to_str(role));
    printf(" Free Heap:           %u bytes\n", (unsigned)esp_get_free_heap_size());
    printf("============================================================\n\n");

    /* 5. Execution Loop */
    int blink_delay_ms = 1000;
    if (role == ROLE_ROOM_1) {
        blink_delay_ms = 500;  /* 2 Hz */
    } else if (role == ROLE_ROOM_2) {
        blink_delay_ms = 250;  /* 4 Hz */
    } else {
        blink_delay_ms = 1000; /* 1 Hz (Entrance) */
    }

    uint32_t iteration = 0;
    while (1) {
        /* Blink LED */
        gpio_set_level(PIN_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(PIN_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(blink_delay_ms - 50));

        iteration++;

        /* Role-specific behavior */
        if (role == ROLE_ROOM_1) {
            if (iteration % 4 == 0) {
                /* Pulse interconnect to Entrance */
                gpio_set_level(PIN_INTERCONNECT_1, 1);
                vTaskDelay(pdMS_TO_TICKS(30));
                gpio_set_level(PIN_INTERCONNECT_1, 0);
                printf("[NODE_ROOM_1 (01)] Pulsed GPIO 18 to Entrance Hub (iter=%lu)\n",
                       (unsigned long)iteration);
            }
        } else if (role == ROLE_ROOM_2) {
            if (iteration % 8 == 0) {
                /* Pulse interconnect to Entrance */
                gpio_set_level(PIN_INTERCONNECT_2, 1);
                vTaskDelay(pdMS_TO_TICKS(30));
                gpio_set_level(PIN_INTERCONNECT_2, 0);
                printf("[NODE_ROOM_2 (10)] Pulsed GPIO 19 to Entrance Hub (iter=%lu)\n",
                       (unsigned long)iteration);
            }
        } else if (role == ROLE_ENTRANCE) {
            /* Check if pulses were received from Room 1 or Room 2 */
            int p1 = gpio_get_level(PIN_INTERCONNECT_1);
            int p2 = gpio_get_level(PIN_INTERCONNECT_2);
            if (p1) {
                printf("[NODE_ENTRANCE (00)] DETECTED INTERCONNECT SIGNAL from Room 1 on GPIO 18!\n");
            }
            if (p2) {
                printf("[NODE_ENTRANCE (00)] DETECTED INTERCONNECT SIGNAL from Room 2 on GPIO 19!\n");
            }
            if (iteration % 3 == 0) {
                printf("[NODE_ENTRANCE (00)] HEARTBEAT: Hub running normally (iter=%lu)\n",
                       (unsigned long)iteration);
            }
        }
    }
}
