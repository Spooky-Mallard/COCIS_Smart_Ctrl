#include "door.h"
#include "board_pins.h"
#include "buzzer.h"
#include "config.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "logger.h"

static const char *TAG = "DOOR";

/* 50 Hz servo (20 ms period) with 13-bit duty (8192 steps):
 * 0 deg (~1.0 ms pulse) = 8192 * (1.0 / 20.0) = 410
 * 90 deg (~1.5 ms pulse) = 8192 * (1.5 / 20.0) = 614
 */
#define SERVO_DUTY_CLOSED_0DEG   410
#define SERVO_DUTY_OPEN_90DEG    614

static door_status_t s_doors[DOOR_ID_COUNT];
static TimerHandle_t s_relock_timers[DOOR_ID_COUNT] = { NULL, NULL };
static TimerHandle_t s_alarm_led_timer = NULL;

static void apply_servo_and_led(door_id_t door, bool open)
{
    ledc_channel_t ch = (door == DOOR_ID_ENTRY) ? BOARD_LEDC_CH_ENTRY_SRV : BOARD_LEDC_CH_EXIT_SRV;
    gpio_num_t led_pin = (door == DOOR_ID_ENTRY) ? PIN_LED_ENTRY : PIN_LED_EXIT;
    uint32_t duty = open ? SERVO_DUTY_OPEN_90DEG : SERVO_DUTY_CLOSED_0DEG;

    ledc_set_duty(BOARD_LEDC_SPEED_MODE, ch, duty);
    ledc_update_duty(BOARD_LEDC_SPEED_MODE, ch);
    gpio_set_level(led_pin, open ? 1 : 0);
}

static void relock_timer_cb(TimerHandle_t xTimer)
{
    intptr_t idx = (intptr_t)pvTimerGetTimerID(xTimer);
    if (idx >= 0 && idx < DOOR_ID_COUNT) {
        if (s_doors[idx].state != DOOR_STATE_EMERGENCY_OPEN) {
            s_doors[idx].state = DOOR_STATE_LOCKED;
            apply_servo_and_led((door_id_t)idx, false);
            ESP_LOGI(TAG, "%s door auto-locked", (idx == DOOR_ID_ENTRY) ? "ENTRY" : "EXIT");
        }
    }
}

static void alarm_led_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (s_doors[DOOR_ID_ENTRY].state != DOOR_STATE_EMERGENCY_OPEN &&
        s_doors[DOOR_ID_EXIT].state != DOOR_STATE_EMERGENCY_OPEN) {
        gpio_set_level(PIN_LED_ALARM, 0);
    }
}

esp_err_t door_init(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode = BOARD_LEDC_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = BOARD_LEDC_SERVO_TIMER,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        return err;
    }

    const gpio_num_t pins[DOOR_ID_COUNT] = { PIN_SERVO_ENTRY, PIN_SERVO_EXIT };
    const ledc_channel_t chs[DOOR_ID_COUNT] = { BOARD_LEDC_CH_ENTRY_SRV, BOARD_LEDC_CH_EXIT_SRV };

    for (int i = 0; i < DOOR_ID_COUNT; i++) {
        ledc_channel_config_t ccfg = {
            .gpio_num = pins[i],
            .speed_mode = BOARD_LEDC_SPEED_MODE,
            .channel = chs[i],
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = BOARD_LEDC_SERVO_TIMER,
            .duty = SERVO_DUTY_CLOSED_0DEG,
            .hpoint = 0
        };
        err = ledc_channel_config(&ccfg);
        if (err != ESP_OK) {
            return err;
        }
        s_doors[i].state = DOOR_STATE_LOCKED;
        s_doors[i].open_count = 0;
        s_doors[i].forced_alarm_count = 0;
        apply_servo_and_led((door_id_t)i, false);

        s_relock_timers[i] = xTimerCreate("door_tmr",
                                          pdMS_TO_TICKS(CFG_DOOR_OPEN_DURATION_MS),
                                          pdFALSE,
                                          (void *)(intptr_t)i,
                                          relock_timer_cb);
    }

    s_alarm_led_timer = xTimerCreate("alm_led", pdMS_TO_TICKS(1200), pdFALSE, NULL, alarm_led_timer_cb);
    ESP_LOGI(TAG, "Entry (GPIO %d) and Exit (GPIO %d) servo door actuators initialized",
             (int)PIN_SERVO_ENTRY, (int)PIN_SERVO_EXIT);
    return ESP_OK;
}

esp_err_t door_unlock_timed(door_id_t door)
{
    if (door < 0 || door >= DOOR_ID_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    sys_config_t cfg;
    config_get(&cfg);

    s_doors[door].open_count++;
    if (s_doors[door].state != DOOR_STATE_EMERGENCY_OPEN) {
        s_doors[door].state = DOOR_STATE_UNLOCKED_TIMED;
    }
    apply_servo_and_led(door, true);

    if (s_relock_timers[door] != NULL) {
        xTimerChangePeriod(s_relock_timers[door], pdMS_TO_TICKS(cfg.door_open_ms), 0);
        xTimerStart(s_relock_timers[door], 0);
    }
    return ESP_OK;
}

esp_err_t door_lock_now(door_id_t door)
{
    if (door < 0 || door >= DOOR_ID_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_relock_timers[door] != NULL) {
        xTimerStop(s_relock_timers[door], 0);
    }
    s_doors[door].state = DOOR_STATE_LOCKED;
    apply_servo_and_led(door, false);
    return ESP_OK;
}

esp_err_t door_set_emergency_all(bool emergency_open)
{
    for (int i = 0; i < DOOR_ID_COUNT; i++) {
        if (s_relock_timers[i] != NULL) {
            xTimerStop(s_relock_timers[i], 0);
        }
        s_doors[i].state = emergency_open ? DOOR_STATE_EMERGENCY_OPEN : DOOR_STATE_LOCKED;
        apply_servo_and_led((door_id_t)i, emergency_open);
    }
    gpio_set_level(PIN_LED_ALARM, emergency_open ? 1 : 0);
    if (emergency_open) {
        buzzer_play(BUZZER_PAT_EMERGENCY);
    }
    return ESP_OK;
}

esp_err_t door_trigger_deny_indicator(void)
{
    gpio_set_level(PIN_LED_ALARM, 1);
    if (s_alarm_led_timer != NULL) {
        xTimerStart(s_alarm_led_timer, 0);
    }
    return ESP_OK;
}

esp_err_t door_simulate_forced_hold(door_id_t door)
{
    if (door < 0 || door >= DOOR_ID_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    s_doors[door].state = DOOR_STATE_FORCED_ALARM;
    s_doors[door].forced_alarm_count++;
    gpio_set_level(PIN_LED_ALARM, 1);
    buzzer_play(BUZZER_PAT_DOOR_ALARM);

    const char *dname = (door == DOOR_ID_ENTRY) ? "ENTRY_DOOR" : "EXIT_DOOR";
    logger_log_audit(dname, "TAILGATE_FORCED_ALARM", "Door held open / forced without valid scan");

    if (s_relock_timers[door] != NULL) {
        xTimerChangePeriod(s_relock_timers[door], pdMS_TO_TICKS(2500), 0);
        xTimerStart(s_relock_timers[door], 0);
    }
    if (s_alarm_led_timer != NULL) {
        xTimerStart(s_alarm_led_timer, 0);
    }
    return ESP_OK;
}

esp_err_t door_get_status(door_id_t door, door_status_t *out_status)
{
    if (door < 0 || door >= DOOR_ID_COUNT || out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_status = s_doors[door];
    return ESP_OK;
}

const char *door_state_to_str(door_state_t state)
{
    switch (state) {
        case DOOR_STATE_LOCKED:         return "LOCKED (Closed)";
        case DOOR_STATE_UNLOCKED_TIMED: return "OPEN (Granted)";
        case DOOR_STATE_EMERGENCY_OPEN: return "EMERGENCY UNLOCKED";
        case DOOR_STATE_FORCED_ALARM:   return "ALARM (Forced/Held)";
        default:                        return "UNKNOWN";
    }
}
