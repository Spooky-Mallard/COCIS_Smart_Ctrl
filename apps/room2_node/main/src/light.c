#include "light.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "ROOM_LIGHT";

static gpio_num_t s_relay_pin = GPIO_NUM_12;
static light_state_change_cb_t s_callback = NULL;

static light_state_t s_state = LIGHT_STATE_OFF_VACANT;
static bool s_relay_on = false;
static bool s_motion_active = false;
static uint8_t s_override_mode = 2; /* 0=force OFF, 1=force ON, 2=AUTO */

static int32_t s_countdown_sec = 0;
static uint32_t s_total_on_sec = 0;
static uint32_t s_total_vacant_sec = 0;

#define GRACE_PERIOD_SEC   5
#define VACANCY_TIMEOUT_SEC 15
#define FIXTURE_WATTS      120

const char *light_state_to_str(light_state_t state)
{
    switch (state) {
    case LIGHT_STATE_OFF_VACANT:    return "OFF_VACANT";
    case LIGHT_STATE_ON_ACTIVE:     return "ON_ACTIVE";
    case LIGHT_STATE_ON_GRACE_HOLD: return "ON_GRACE_HOLD";
    default:                        return "UNKNOWN";
    }
}

static void apply_relay(bool on)
{
    s_relay_on = on;
    gpio_set_level(s_relay_pin, on ? 1 : 0);
}

esp_err_t light_init(gpio_num_t relay_pin, light_state_change_cb_t cb)
{
    s_relay_pin = relay_pin;
    s_callback = cb;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << relay_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    apply_relay(false);
    s_state = LIGHT_STATE_OFF_VACANT;

    ESP_LOGI(TAG, "Lecture room lighting FSM initialized on GPIO %d", (int)relay_pin);
    return ESP_OK;
}

void light_handle_motion(bool motion_active)
{
    s_motion_active = motion_active;

    if (s_override_mode == 0) {
        apply_relay(false);
        return;
    }
    if (s_override_mode == 1) {
        apply_relay(true);
        return;
    }

    /* AUTO Mode */
    if (motion_active) {
        if (s_state != LIGHT_STATE_ON_ACTIVE) {
            s_state = LIGHT_STATE_ON_ACTIVE;
            apply_relay(true);
            ESP_LOGI(TAG, "FSM: Motion detected -> Transitioned to ON_ACTIVE");
            if (s_callback) {
                s_callback(s_state, true, s_total_on_sec, light_get_energy_saved_mwh());
            }
        }
        s_countdown_sec = 0;
    } else {
        if (s_state == LIGHT_STATE_ON_ACTIVE) {
            s_state = LIGHT_STATE_ON_GRACE_HOLD;
            s_countdown_sec = GRACE_PERIOD_SEC + VACANCY_TIMEOUT_SEC;
            ESP_LOGI(TAG, "FSM: Motion cleared -> Entering GRACE_HOLD (%ld s countdown)", (long)s_countdown_sec);
        }
    }
}

void light_tick_1hz(void)
{
    if (s_relay_on) {
        s_total_on_sec++;
    } else {
        s_total_vacant_sec++;
    }

    if (s_override_mode != 2) {
        return;
    }

    if (s_state == LIGHT_STATE_ON_GRACE_HOLD) {
        if (s_countdown_sec > 0) {
            s_countdown_sec--;
        }
        if (s_countdown_sec == 0) {
            s_state = LIGHT_STATE_OFF_VACANT;
            apply_relay(false);
            ESP_LOGI(TAG, "FSM: Vacancy timeout reached -> Lights switched OFF (Energy saved: %lu mWh)",
                     (unsigned long)light_get_energy_saved_mwh());
            if (s_callback) {
                s_callback(s_state, false, s_total_on_sec, light_get_energy_saved_mwh());
            }
        }
    }
}

void light_set_override(uint8_t override_val)
{
    s_override_mode = override_val;
    if (override_val == 0) {
        apply_relay(false);
        s_state = LIGHT_STATE_OFF_VACANT;
        ESP_LOGI(TAG, "Remote OVERRIDE: Lights forced OFF");
    } else if (override_val == 1) {
        apply_relay(true);
        s_state = LIGHT_STATE_ON_ACTIVE;
        ESP_LOGI(TAG, "Remote OVERRIDE: Lights forced ON");
    } else {
        ESP_LOGI(TAG, "Remote OVERRIDE: Released back to AUTO control");
        light_handle_motion(s_motion_active);
    }
}

light_state_t light_get_state(void)
{
    return s_state;
}

bool light_is_relay_on(void)
{
    return s_relay_on;
}

uint32_t light_get_on_seconds(void)
{
    return s_total_on_sec;
}

uint32_t light_get_energy_saved_mwh(void)
{
    /* Energy saved when lights are off: (vacant_sec * 120W * 1000) / 3600 = vacant_sec * 33.33 mWh */
    return (uint32_t)((uint64_t)s_total_vacant_sec * FIXTURE_WATTS * 1000ULL / 3600ULL);
}
