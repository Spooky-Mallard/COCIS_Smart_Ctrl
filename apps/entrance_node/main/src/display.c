#include "display.h"
#include <stdio.h>
#include <string.h>
#include "board_pins.h"
#include "config.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "light.h"
#include "occupancy_ledger.h"
#include "schedule.h"
#include "sync.h"
#include "time_sync.h"

static const char *TAG = "DISPLAY";

#define LCD_BACKLIGHT 0x08
#define LCD_ENABLE    0x04
#define LCD_RS_DATA   0x01

static i2c_master_dev_handle_t s_lcd_dev = NULL;
static bool                    s_lcd_ready = false;
static char                    s_alert_line0[17] = { 0 };
static char                    s_alert_line1[17] = { 0 };
static int64_t                 s_alert_until_ms = 0;

static void lcd_write_nibble(uint8_t nibble, uint8_t rs_flag)
{
    if (!s_lcd_ready || s_lcd_dev == NULL) {
        return;
    }
    uint8_t base = (uint8_t)((nibble << 4) | (rs_flag ? LCD_RS_DATA : 0x00) | LCD_BACKLIGHT);
    uint8_t pulse[2] = { (uint8_t)(base | LCD_ENABLE), base };
    i2c_master_transmit(s_lcd_dev, pulse, sizeof(pulse), 50);
    esp_rom_delay_us(50);
}

static void lcd_write_byte(uint8_t val, uint8_t rs_flag)
{
    lcd_write_nibble((uint8_t)(val >> 4), rs_flag);
    lcd_write_nibble((uint8_t)(val & 0x0F), rs_flag);
}

static void lcd_cmd(uint8_t cmd)
{
    lcd_write_byte(cmd, 0);
    if (cmd == 0x01 || cmd == 0x02) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void lcd_draw_row(uint8_t row, const char *text)
{
    uint8_t addr = (row == 0) ? 0x80 : 0xC0;
    lcd_cmd(addr);

    char padded[17];
    snprintf(padded, sizeof(padded), "%-16.16s", text ? text : "");
    for (int i = 0; i < 16; i++) {
        lcd_write_byte((uint8_t)padded[i], 1);
    }
}

static void display_task(void *arg)
{
    (void)arg;
    uint8_t page = 0;
    int64_t last_page_switch_ms = 0;
    char prev0[17] = { 0 };
    char prev1[17] = { 0 };

    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000LL;
        char line0[17] = { 0 };
        char line1[17] = { 0 };

        if (sync_lock(LOCK_LEVEL_DISPLAY, 100) == ESP_OK) {
            if (schedule_is_emergency_mode()) {
                uint32_t inside = occupancy_ledger_get_count();
                snprintf(line0, sizeof(line0), "!! EMERGENCY !! ");
                snprintf(line1, sizeof(line1), "EVAC! IN:%lu      ", (unsigned long)inside);
            } else if (now_ms < s_alert_until_ms) {
                snprintf(line0, sizeof(line0), "%-16.16s", s_alert_line0);
                snprintf(line1, sizeof(line1), "%-16.16s", s_alert_line1);
            } else {
                if ((now_ms - last_page_switch_ms) >= 3000) {
                    page = (uint8_t)((page + 1) % 3);
                    last_page_switch_ms = now_ms;
                }

                if (page == 0) {
                    uint32_t inside = occupancy_ledger_get_count();
                    bool open = schedule_is_open_now();
                    snprintf(line0, sizeof(line0), "CoCIS Smart Ctrl");
                    snprintf(line1, sizeof(line1), "IN:%-2lu  SCHED:%s",
                             (unsigned long)inside, open ? "OPN" : "CLS");
                } else if (page == 1) {
                    room_light_status_t r1 = { 0 }, r2 = { 0 };
                    light_get_room_status(1, &r1);
                    light_get_room_status(2, &r2);
                    snprintf(line0, sizeof(line0), "R1:%s L1:%s   ",
                             r1.pir_motion_active ? "OCC" : "VAC",
                             r1.relay_on ? "ON " : "OFF");
                    snprintf(line1, sizeof(line1), "R2:%s L2:%s   ",
                             r2.pir_motion_active ? "OCC" : "VAC",
                             r2.relay_on ? "ON " : "OFF");
                } else {
                    time_sync_format_lcd(line0, sizeof(line0));
                    uint32_t saved = light_get_total_energy_saved_mwh();
                    snprintf(line1, sizeof(line1), "Saved:%5lumWh  ", (unsigned long)saved);
                }
            }

            if (strcmp(line0, prev0) != 0 || strcmp(line1, prev1) != 0) {
                lcd_draw_row(0, line0);
                lcd_draw_row(1, line1);
                strncpy(prev0, line0, sizeof(prev0) - 1);
                strncpy(prev1, line1, sizeof(prev1) - 1);
            }

            sync_unlock(LOCK_LEVEL_DISPLAY);
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

esp_err_t display_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BOARD_I2C_PORT,
        .scl_io_num = PIN_I2C_SCL,
        .sda_io_num = PIN_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };
    i2c_master_bus_handle_t bus_handle = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus init failed (%s) - continuing without LCD", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_LCD_I2C_ADDR,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ
    };
    err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_lcd_dev);
    if (err != ESP_OK) {
        return err;
    }

    s_lcd_ready = true;
    vTaskDelay(pdMS_TO_TICKS(50));
    lcd_write_nibble(0x03, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(0x03, 0);
    esp_rom_delay_us(150);
    lcd_write_nibble(0x03, 0);
    esp_rom_delay_us(150);
    lcd_write_nibble(0x02, 0);
    esp_rom_delay_us(150);

    lcd_cmd(0x28); /* 4-bit, 2 lines, 5x8 font */
    lcd_cmd(0x08); /* Display off */
    lcd_cmd(0x01); /* Clear display */
    vTaskDelay(pdMS_TO_TICKS(2));
    lcd_cmd(0x06); /* Entry mode: increment */
    lcd_cmd(0x0C); /* Display on, cursor off */

    lcd_draw_row(0, "CoCIS Smart Ctrl");
    lcd_draw_row(1, "Booting System..");

    if (xTaskCreate(display_task, "display_task", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "16x2 I2C LCD initialized at 0x%02X (SDA=%d, SCL=%d)",
             BOARD_LCD_I2C_ADDR, (int)PIN_I2C_SDA, (int)PIN_I2C_SCL);
    return ESP_OK;
}

esp_err_t display_show_alert(const char *line0, const char *line1, uint32_t duration_ms)
{
    if (sync_lock(LOCK_LEVEL_DISPLAY, CFG_LOCK_TIMEOUT_MS) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    snprintf(s_alert_line0, sizeof(s_alert_line0), "%-16.16s", line0 ? line0 : "");
    snprintf(s_alert_line1, sizeof(s_alert_line1), "%-16.16s", line1 ? line1 : "");
    s_alert_until_ms = (esp_timer_get_time() / 1000LL) + duration_ms;

    sync_unlock(LOCK_LEVEL_DISPLAY);
    return ESP_OK;
}
