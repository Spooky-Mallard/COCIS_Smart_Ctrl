#include "access_control.h"
#include "auth.h"
#include "board_pins.h"
#include "buzzer.h"
#include "cli.h"
#include "config.h"
#include "display.h"
#include "door.h"
#include "esp_err.h"
#include "esp_log.h"
#include "health.h"
#include "light.h"
#include "logger.h"
#include "net_protocol.h"
#include "net_wifi.h"
#include "occupancy_ledger.h"
#include "pir_sensor.h"
#include "rfid.h"
#include "schedule.h"
#include "storage_sd.h"
#include "sync.h"
#include "time_sync.h"
#include "user_db.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Booting %s (%s)", CFG_SYSTEM_NAME, CFG_FIRMWARE_VERSION);

    /* 1. Core Synchronization, NVS Config & Board Pins */
    ESP_ERROR_CHECK(sync_init());
    ESP_ERROR_CHECK(config_init());
    ESP_ERROR_CHECK(board_pins_init());

    /* 2. Time Synchronization (EAT UTC+3), MicroSD Storage & Async Logger */
    ESP_ERROR_CHECK(time_sync_init());
    (void)storage_sd_init();
    ESP_ERROR_CHECK(logger_init());

    /* 3. Authoritative Databases & Security Subsystems */
    ESP_ERROR_CHECK(user_db_init());
    ESP_ERROR_CHECK(schedule_init());
    ESP_ERROR_CHECK(occupancy_ledger_init());
    ESP_ERROR_CHECK(auth_init());

    /* 4. Actuators, Display, Access Control, RFID & Lecture Room Lighting */
    ESP_ERROR_CHECK(buzzer_init());
    (void)display_init();
    ESP_ERROR_CHECK(door_init());
    ESP_ERROR_CHECK(access_control_init());
    ESP_ERROR_CHECK(rfid_init(access_control_handle_scan, access_control_handle_emergency_btn));
    ESP_ERROR_CHECK(light_init());
    ESP_ERROR_CHECK(pir_sensor_init());

    /* 5. Distributed Networking, Health Watchdog & Interactive Serial CLI */
    (void)net_wifi_init();
    ESP_ERROR_CHECK(net_protocol_init());
    ESP_ERROR_CHECK(health_init());
    ESP_ERROR_CHECK(cli_init());

    logger_log_audit("SYSTEM", "BOOT_COMPLETE", "All 21 CoCIS modules active");
    ESP_LOGI(TAG, "All 21 modules initialized and running.");
}
