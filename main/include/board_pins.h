#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_common.h"
#include "esp_err.h"

/* I2C Bus for 16x2 HD44780 LCD (PCF8574 Backpack) */
#define PIN_I2C_SDA             GPIO_NUM_21
#define PIN_I2C_SCL             GPIO_NUM_22
#define BOARD_I2C_PORT          I2C_NUM_0
#define BOARD_LCD_I2C_ADDR      0x27
#define BOARD_I2C_FREQ_HZ       100000

/* SPI2 Bus for MicroSD Card (SDSPI + FATFS) */
#define PIN_SD_CS               GPIO_NUM_5
#define PIN_SD_SCK              GPIO_NUM_18
#define PIN_SD_MOSI             GPIO_NUM_23
#define PIN_SD_MISO             GPIO_NUM_19
#define BOARD_SD_SPI_HOST       SPI2_HOST

/* Main Entrance Servo Actuators & Status LEDs */
#define PIN_SERVO_ENTRY         GPIO_NUM_13
#define PIN_SERVO_EXIT          GPIO_NUM_12
#define PIN_LED_ENTRY           GPIO_NUM_2
#define PIN_LED_EXIT            GPIO_NUM_4
#define PIN_LED_ALARM           GPIO_NUM_15
#define PIN_BUZZER              GPIO_NUM_14

/* Lecture Room PIR Motion Sensors & Light Relays/LEDs */
#define PIN_PIR_ROOM1           GPIO_NUM_34
#define PIN_PIR_ROOM2           GPIO_NUM_35
#define PIN_LIGHT_ROOM1         GPIO_NUM_25
#define PIN_LIGHT_ROOM2         GPIO_NUM_26

/* Physical Interactive Pushbuttons (Active-LOW with Internal Pull-Up) */
#define PIN_BTN_ENTRY_SCAN      GPIO_NUM_27
#define PIN_BTN_EXIT_SCAN       GPIO_NUM_32
#define PIN_BTN_INTRUDER_SCAN   GPIO_NUM_33
#define PIN_BTN_EMERGENCY       GPIO_NUM_16

/* LEDC Timer & Channel Allocation */
#define BOARD_LEDC_SPEED_MODE   LEDC_LOW_SPEED_MODE
#define BOARD_LEDC_SERVO_TIMER  LEDC_TIMER_0
#define BOARD_LEDC_BUZZER_TIMER LEDC_TIMER_1

#define BOARD_LEDC_CH_ENTRY_SRV LEDC_CHANNEL_0
#define BOARD_LEDC_CH_EXIT_SRV  LEDC_CHANNEL_1
#define BOARD_LEDC_CH_BUZZER    LEDC_CHANNEL_2

esp_err_t board_pins_init(void);

#endif
