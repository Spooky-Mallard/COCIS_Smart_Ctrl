#include "room_cli.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "cocis_wifi.h"
#include "cocis_mqtt.h"
#include "cocis_health.h"
#include "pir_sensor.h"
#include "light.h"
#include "room_buffer.h"

static const char *TAG = "ROOM_CLI";

static void print_status(void)
{
    char ip[16] = { 0 };
    cocis_wifi_get_ip_str(ip, sizeof(ip));

    printf("\n==================================================\n");
    printf("         LECTURE ROOM TERMINAL DIAGNOSTICS        \n");
    printf("==================================================\n");
    printf(" PIR Sensor Input:      %s\n", pir_sensor_is_motion_active() ? "MOTION DETECTED" : "IDLE (VACANT)");
    printf(" Lighting Fixture:      %s\n", light_is_relay_on() ? "RELAY ON (LIGHTS POWERED)" : "RELAY OFF");
    printf(" FSM State:             %s\n", light_state_to_str(light_get_state()));
    printf(" Cumulative Active:     %lu sec\n", (unsigned long)light_get_on_seconds());
    printf(" Energy Saved:          %lu mWh\n", (unsigned long)light_get_energy_saved_mwh());
    printf(" Store & Forward Queue: %u / 32 entries\n", (unsigned)room_buffer_count());
    printf(" Wi-Fi Status:          %s (IP: %s, RSSI: %d dBm)\n",
           cocis_wifi_is_connected() ? "CONNECTED" : "OFFLINE", ip, (int)cocis_wifi_get_rssi());
    printf(" MQTT Broker Session:   %s\n", cocis_mqtt_is_connected() ? "ONLINE" : "DISCONNECTED");
    printf(" System Uptime:         %lu sec\n", (unsigned long)cocis_uptime_sec());
    printf(" Free Heap:             %u bytes\n", (unsigned)esp_get_free_heap_size());
    printf("==================================================\n\n");
}

static void cli_task(void *arg)
{
    (void)arg;
    char line[64];
    size_t pos = 0;

    printf("\n[Room Terminal] Diagnostic console ready. Type 'status', 'motion', 'clear', or 'help'.\nroom> ");
    fflush(stdout);

    while (1) {
        int c = getchar();
        if (c == EOF || c < 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (pos > 0) {
                line[pos] = '\0';
                printf("\n");

                if (strcmp(line, "status") == 0) {
                    print_status();
                } else if (strcmp(line, "motion") == 0) {
                    pir_sensor_simulate(true);
                    printf("OK: Simulated motion trigger injected\n");
                } else if (strcmp(line, "clear") == 0) {
                    pir_sensor_simulate(false);
                    printf("OK: Simulated motion cleared\n");
                } else if (strcmp(line, "help") == 0) {
                    printf("Available commands:\n");
                    printf("  status - Display room sensors, relay, buffer depth, and network health\n");
                    printf("  motion - Simulate PIR motion detection\n");
                    printf("  clear  - Clear PIR motion detection (start vacancy countdown)\n");
                    printf("  help   - Show this reference\n");
                } else {
                    printf("Unknown command: '%s'. Type 'help' for options.\n", line);
                }
                pos = 0;
            }
            printf("room> ");
            fflush(stdout);
        } else if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
        } else if (pos < sizeof(line) - 1 && c >= 32 && c <= 126) {
            line[pos++] = (char)c;
            putchar(c);
            fflush(stdout);
        }
    }
}

esp_err_t room_cli_init(void)
{
    xTaskCreate(cli_task, "room_cli", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "Diagnostic console task spawned");
    return ESP_OK;
}
