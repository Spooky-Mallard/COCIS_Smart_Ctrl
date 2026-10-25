#include "light.h"
#include <string.h>
#include "net_protocol.h"
#include "esp_log.h"

static const char *TAG = "ENTRANCE_LIGHT_PROXY";

esp_err_t light_init(void)
{
    ESP_LOGI(TAG, "Entrance Hub lighting proxy initialized (network-backed)");
    return ESP_OK;
}

const char *light_fsm_to_str(light_fsm_state_t state)
{
    switch (state) {
    case LIGHT_FSM_OFF_VACANT:   return "OFF_VACANT";
    case LIGHT_FSM_ON_ACTIVE:    return "ON_ACTIVE";
    case LIGHT_FSM_ON_GRACE_HOLD: return "ON_GRACE_HOLD";
    default:                     return "UNKNOWN";
    }
}

esp_err_t light_get_room_status(uint8_t room_id, room_light_status_t *out_status)
{
    if (!out_status || room_id < 1 || room_id > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_status, 0, sizeof(room_light_status_t));
    out_status->room_id = room_id;

    const entrance_room_telemetry_t *telem = net_protocol_get_room_telemetry(room_id);
    if (telem) {
        out_status->pir_motion_active = (telem->last_room_occupied != 0);
        out_status->relay_on = (telem->last_light_state != 0);
        out_status->fsm_state = (light_fsm_state_t)telem->last_light_state;
    }
    return ESP_OK;
}

uint32_t light_get_total_energy_saved_mwh(void)
{
    return 1450; /* Base simulated energy saved */
}

void light_tick_1hz(void)
{
}

esp_err_t light_notify_motion(uint8_t room_id, bool motion_detected)
{
    (void)room_id;
    (void)motion_detected;
    return ESP_OK;
}
