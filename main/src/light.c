#include "light.h"
#include "board_pins.h"
#include "config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "logger.h"
#include "net_protocol.h"
#include "sync.h"

static const char *TAG = "LIGHT";

static room_light_status_t s_rooms[CFG_ROOM_COUNT];

static void set_room_relay_hw(uint8_t room_id, bool on)
{
    gpio_num_t pin = (room_id == 1) ? PIN_LIGHT_ROOM1 : PIN_LIGHT_ROOM2;
    gpio_set_level(pin, on ? 1 : 0);
}

esp_err_t light_init(void)
{
    esp_err_t err = sync_lock(LOCK_LEVEL_ROOM, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    for (uint8_t i = 0; i < CFG_ROOM_COUNT; i++) {
        s_rooms[i].room_id = (uint8_t)(i + 1);
        s_rooms[i].pir_motion_active = false;
        s_rooms[i].relay_on = false;
        s_rooms[i].fsm_state = LIGHT_FSM_OFF_VACANT;
        s_rooms[i].seconds_until_off = 0;
        s_rooms[i].total_light_on_sec = 0;
        s_rooms[i].total_occupied_sec = 0;
        s_rooms[i].total_vacant_off_sec = 0;
        s_rooms[i].energy_saved_mwh = 0;
        s_rooms[i].transition_count = 0;
        set_room_relay_hw((uint8_t)(i + 1), false);
    }

    sync_unlock(LOCK_LEVEL_ROOM);
    ESP_LOGI(TAG, "Automated lighting FSM initialized for Lecture Rooms 1 & 2");
    return ESP_OK;
}

esp_err_t light_notify_motion(uint8_t room_id, bool motion_detected)
{
    if (room_id < 1 || room_id > CFG_ROOM_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    sys_config_t cfg;
    config_get(&cfg);

    esp_err_t err = sync_lock(LOCK_LEVEL_ROOM, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    room_light_status_t *rm = &s_rooms[room_id - 1];
    bool prev_relay = rm->relay_on;
    light_fsm_state_t prev_fsm = rm->fsm_state;
    rm->pir_motion_active = motion_detected;

    if (motion_detected) {
        rm->fsm_state = LIGHT_FSM_ON_ACTIVE;
        rm->relay_on = true;
        rm->seconds_until_off = cfg.light_vacancy_sec + cfg.light_grace_sec;
        set_room_relay_hw(room_id, true);
    } else if (rm->relay_on) {
        rm->fsm_state = LIGHT_FSM_ON_GRACE_HOLD;
        rm->seconds_until_off = cfg.light_vacancy_sec;
    }

    bool state_changed = (prev_relay != rm->relay_on) || (prev_fsm != rm->fsm_state);
    if (state_changed) {
        rm->transition_count++;
    }

    room_light_status_t snap = *rm;
    sync_unlock(LOCK_LEVEL_ROOM);

    if (state_changed) {
        ESP_LOGI(TAG, "Lecture Room %u -> %s (Relay=%s, Timeout=%lu s)",
                 (unsigned)room_id,
                 light_fsm_to_str(snap.fsm_state),
                 snap.relay_on ? "ON" : "OFF",
                 (unsigned long)snap.seconds_until_off);
        net_protocol_send_room_event(room_id,
                                     snap.pir_motion_active,
                                     (uint8_t)snap.fsm_state,
                                     snap.total_light_on_sec,
                                     snap.energy_saved_mwh);
    }
    return ESP_OK;
}

void light_tick_1hz(void)
{
    sys_config_t cfg;
    config_get(&cfg);

    /* Power saved per second of lights auto-switched OFF:
     * Watts * (1000 mW / W) / (3600 s / hr) = Watts * 1000 / 3600 mWh/s
     * For 120W fixture, 1 second off saves ~33 mWh.
     */
    uint32_t mwh_per_sec = (cfg.room_watts * 1000U) / 3600U;
    if (mwh_per_sec == 0) {
        mwh_per_sec = 33;
    }

    for (uint8_t i = 0; i < CFG_ROOM_COUNT; i++) {
        bool send_off_event = false;
        room_light_status_t snap = { 0 };

        if (sync_lock(LOCK_LEVEL_ROOM, CFG_LOCK_TIMEOUT_MS) == ESP_OK) {
            room_light_status_t *rm = &s_rooms[i];
            if (rm->pir_motion_active) {
                rm->total_occupied_sec++;
                rm->total_light_on_sec++;
                rm->seconds_until_off = cfg.light_vacancy_sec + cfg.light_grace_sec;
            } else if (rm->relay_on) {
                rm->total_light_on_sec++;
                if (rm->seconds_until_off > 0) {
                    rm->seconds_until_off--;
                }
                if (rm->seconds_until_off == 0) {
                    rm->relay_on = false;
                    rm->fsm_state = LIGHT_FSM_OFF_VACANT;
                    rm->transition_count++;
                    set_room_relay_hw(rm->room_id, false);
                    send_off_event = true;
                }
            } else {
                rm->total_vacant_off_sec++;
                rm->energy_saved_mwh += mwh_per_sec;
            }
            snap = *rm;
            sync_unlock(LOCK_LEVEL_ROOM);
        }

        if (send_off_event) {
            ESP_LOGI(TAG, "Lecture Room %u vacancy timeout expired -> Light switched OFF (Saved=%lu mWh)",
                     (unsigned)snap.room_id, (unsigned long)snap.energy_saved_mwh);
            net_protocol_send_room_event(snap.room_id,
                                         false,
                                         (uint8_t)LIGHT_FSM_OFF_VACANT,
                                         snap.total_light_on_sec,
                                         snap.energy_saved_mwh);
        }
    }
}

esp_err_t light_get_room_status(uint8_t room_id, room_light_status_t *out_status)
{
    if (room_id < 1 || room_id > CFG_ROOM_COUNT || out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_ROOM, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    *out_status = s_rooms[room_id - 1];
    sync_unlock(LOCK_LEVEL_ROOM);
    return ESP_OK;
}

uint32_t light_get_total_energy_saved_mwh(void)
{
    uint32_t total = 0;
    if (sync_lock(LOCK_LEVEL_ROOM, CFG_LOCK_TIMEOUT_MS) == ESP_OK) {
        for (uint8_t i = 0; i < CFG_ROOM_COUNT; i++) {
            total += s_rooms[i].energy_saved_mwh;
        }
        sync_unlock(LOCK_LEVEL_ROOM);
    }
    return total;
}

const char *light_fsm_to_str(light_fsm_state_t state)
{
    switch (state) {
        case LIGHT_FSM_OFF_VACANT:    return "OFF_VACANT";
        case LIGHT_FSM_ON_ACTIVE:     return "ON_ACTIVE";
        case LIGHT_FSM_ON_GRACE_HOLD: return "GRACE_HOLD";
        default:                      return "UNKNOWN";
    }
}
