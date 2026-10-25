#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cocis_types.h"
#include "cocis_config.h"
#include "cocis_crypto.h"
#include "cocis_sync.h"
#include "cocis_time.h"
#include "cocis_wifi.h"
#include "cocis_mqtt.h"
#include "cocis_health.h"

#include "board_pins.h"
#include "display.h"
#include "buzzer.h"
#include "door.h"
#include "storage_sd.h"
#include "user_db.h"
#include "schedule.h"
#include "occupancy_ledger.h"
#include "logger.h"
#include "access_control.h"
#include "auth.h"
#include "rfid.h"
#include "net_protocol.h"
#include "light.h"
#include "pir_sensor.h"
#include "cli.h"

static const char *TAG = "ENTRANCE_MAIN";

void app_main(void)
{
    printf("\n");
    printf("====================================================================\n");
    printf("    CoCIS SMART CONTROL SYSTEM - MAIN ENTRANCE COORDINATOR (HUB)    \n");
    printf("    Makerere University | BSE III & CS II Coursework Project 2026   \n");
    printf("    Firmware: v2.0.0-HUB | Target: ESP32 | Simulator: Wokwi         \n");
    printf("====================================================================\n\n");

    /* 1. Core Synchronization & Cryptographic Systems */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    cocis_sync_init();
    cocis_crypto_init();
    cocis_time_init();
    cocis_health_init(NODE_ID_ENTRANCE);
    config_init();

    /* 2. Board Peripherals */
    board_pins_init();
    display_init();
    buzzer_init();
    door_init();

    /* 3. Authoritative Storage & Business Logic */
    storage_sd_init();
    user_db_init();
    schedule_init();
    occupancy_ledger_init();
    auth_init();
    logger_init();
    access_control_init();
    rfid_init(access_control_handle_scan, access_control_handle_emergency_btn);

    /* 4. Distributed Network & Transport */
    cocis_wifi_init();
    cocis_mqtt_init(NODE_ID_ENTRANCE);
    net_protocol_init();
    light_init();
    pir_sensor_init();

    /* 5. Serial Interactive Console */
    cli_init();

    ESP_LOGI(TAG, "Entrance Hub fully initialized and listening for room telemetry");

    /* 6. Background Coordinator Task */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
