#include "access_control.h"
#include <stdio.h>
#include <string.h>
#include "buzzer.h"
#include "config.h"
#include "display.h"
#include "door.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "logger.h"
#include "net_protocol.h"
#include "occupancy_ledger.h"
#include "schedule.h"
#include "time_sync.h"
#include "user_db.h"

static const char *TAG = "ACCESS_CTRL";

static QueueHandle_t  s_scan_queue = NULL;
static access_stats_t s_stats = { 0 };

static access_result_t process_scan_event(const rfid_scan_event_t *evt)
{
    bool is_exit = (evt->reader == RFID_READER_EXIT);
    const char *dir_str = is_exit ? "EXIT" : "ENTRY";
    time_snapshot_t now;
    time_sync_get_now(&now);

    /* 1. Emergency Evacuation Mode Egress Check */
    if (schedule_is_emergency_mode() && is_exit) {
        uint32_t new_cnt = occupancy_ledger_get_count();
        occupancy_ledger_check_out(evt->uid, now.epoch, &new_cnt);
        door_set_emergency_all(true);
        buzzer_play(BUZZER_PAT_GRANT);
        s_stats.granted_exit++;
        s_stats.last_result = ACCESS_RESULT_GRANTED_EMERGENCY_EVAC;
        logger_log_access(evt->uid, dir_str, "GRANT_EMERGENCY_EVAC", (int32_t)new_cnt);
        display_show_alert("EVAC EXIT OK", evt->uid, 2000);
        return ACCESS_RESULT_GRANTED_EMERGENCY_EVAC;
    }

    /* 2. Lookup User in Enrolled Database */
    user_record_t user;
    if (user_db_find(evt->uid, &user) != ESP_OK) {
        s_stats.denied_unknown++;
        s_stats.last_result = ACCESS_RESULT_DENIED_UNKNOWN_TAG;
        door_trigger_deny_indicator();
        buzzer_play(BUZZER_PAT_DENY);
        logger_log_access(evt->uid, dir_str, "DENY_UNKNOWN_TAG", (int32_t)occupancy_ledger_get_count());
        display_show_alert("DENY: UNKNOWN ID", evt->uid, 2500);
        ESP_LOGW(TAG, "[%s] DENIED unknown tag %s", dir_str, evt->uid);
        return ACCESS_RESULT_DENIED_UNKNOWN_TAG;
    }

    /* 3. Verify Account Active Status */
    if (!user.active) {
        s_stats.denied_inactive++;
        s_stats.last_result = ACCESS_RESULT_DENIED_USER_INACTIVE;
        door_trigger_deny_indicator();
        buzzer_play(BUZZER_PAT_DENY);
        logger_log_access(user.uid, dir_str, "DENY_USER_INACTIVE", (int32_t)occupancy_ledger_get_count());
        display_show_alert("DENY: INACTIVE", user.name, 2500);
        ESP_LOGW(TAG, "[%s] DENIED inactive user %s (%s)", dir_str, user.uid, user.name);
        return ACCESS_RESULT_DENIED_USER_INACTIVE;
    }

    /* 4. Verify Visitor Expiry */
    if (user.expiry_epoch > 0 && now.epoch > user.expiry_epoch) {
        s_stats.denied_expired++;
        s_stats.last_result = ACCESS_RESULT_DENIED_VISITOR_EXPIRED;
        door_trigger_deny_indicator();
        buzzer_play(BUZZER_PAT_DENY);
        logger_log_access(user.uid, dir_str, "DENY_VISITOR_EXPIRED", (int32_t)occupancy_ledger_get_count());
        display_show_alert("DENY: EXPIRED", user.name, 2500);
        ESP_LOGW(TAG, "[%s] DENIED expired visitor %s (%s)", dir_str, user.uid, user.name);
        return ACCESS_RESULT_DENIED_VISITOR_EXPIRED;
    }

    /* 5. Verify Anti-Passback FIRST so passback violations are always caught */
    bool currently_inside = occupancy_ledger_is_inside(user.uid);
    if (!is_exit && currently_inside) {
        s_stats.denied_antipassback++;
        s_stats.last_result = ACCESS_RESULT_DENIED_ANTIPASSBACK_ENTRY;
        occupancy_ledger_record_apb_violation();
        door_trigger_deny_indicator();
        buzzer_play(BUZZER_PAT_ANTIPASSBACK);
        logger_log_access(user.uid, dir_str, "DENY_ANTIPASSBACK_IN", (int32_t)occupancy_ledger_get_count());
        display_show_alert("DENY: PASSBACK", "Already Inside!", 2500);
        ESP_LOGW(TAG, "[ENTRY] DENIED anti-passback: %s (%s) is already inside", user.uid, user.name);
        return ACCESS_RESULT_DENIED_ANTIPASSBACK_ENTRY;
    }
    if (is_exit && !currently_inside) {
        s_stats.denied_antipassback++;
        s_stats.last_result = ACCESS_RESULT_DENIED_ANTIPASSBACK_EXIT;
        occupancy_ledger_record_apb_violation();
        door_trigger_deny_indicator();
        buzzer_play(BUZZER_PAT_ANTIPASSBACK);
        logger_log_access(user.uid, dir_str, "DENY_ANTIPASSBACK_OUT", (int32_t)occupancy_ledger_get_count());
        display_show_alert("DENY: PASSBACK", "Not Inside!", 2500);
        ESP_LOGW(TAG, "[EXIT] DENIED anti-passback: %s (%s) is not recorded inside", user.uid, user.name);
        return ACCESS_RESULT_DENIED_ANTIPASSBACK_EXIT;
    }

    /* 6. Evaluate Building Access Schedule (both Entry & Exit enforced) */
    schedule_decision_t sdec = schedule_evaluate(is_exit, user.role, user.uid);
    if (sdec != SCHED_ALLOW_NORMAL &&
        sdec != SCHED_ALLOW_ADMIN_BYPASS &&
        sdec != SCHED_ALLOW_SUPERVISOR_OVERRIDE &&
        sdec != SCHED_ALLOW_EMERGENCY_EXIT) {

        door_trigger_deny_indicator();
        buzzer_play(BUZZER_PAT_DENY);

        if (is_exit) {
            s_stats.denied_schedule_exit++;
            s_stats.last_result = ACCESS_RESULT_DENIED_OUTSIDE_SCHED_EXIT;
            logger_log_access(user.uid, dir_str, schedule_decision_to_str(sdec), (int32_t)occupancy_ledger_get_count());
            display_show_alert("DENY EXIT: SCHED", "Need Supv. Pass", 2500);
            ESP_LOGW(TAG, "[EXIT] DENIED after-hours exit for %s (%s) - supervisor override required",
                     user.uid, user.name);
            return ACCESS_RESULT_DENIED_OUTSIDE_SCHED_EXIT;
        } else {
            s_stats.denied_schedule_entry++;
            s_stats.last_result = (sdec == SCHED_DENY_EMERGENCY_LOCKDOWN_ENTRY)
                                  ? ACCESS_RESULT_DENIED_EMERGENCY_LOCKDOWN
                                  : ACCESS_RESULT_DENIED_OUTSIDE_SCHED_ENTRY;
            logger_log_access(user.uid, dir_str, schedule_decision_to_str(sdec), (int32_t)occupancy_ledger_get_count());
            display_show_alert("DENY: CLOSED HRS", user.name, 2500);
            ESP_LOGW(TAG, "[ENTRY] DENIED outside schedule for %s (%s)", user.uid, user.name);
            return s_stats.last_result;
        }
    }

    /* 7. Commit Occupancy Ledger Update & Actuate Door */
    uint32_t new_count = 0;
    if (!is_exit) {
        occupancy_ledger_check_in(&user, now.epoch, &new_count);
        door_unlock_timed(DOOR_ID_ENTRY);
        s_stats.granted_entry++;
    } else {
        occupancy_ledger_check_out(user.uid, now.epoch, &new_count);
        door_unlock_timed(DOOR_ID_EXIT);
        s_stats.granted_exit++;
    }

    buzzer_play(BUZZER_PAT_GRANT);

    access_result_t final_res = ACCESS_RESULT_GRANTED_NORMAL;
    const char *log_reason = "GRANT_NORMAL";
    if (sdec == SCHED_ALLOW_ADMIN_BYPASS) {
        final_res = ACCESS_RESULT_GRANTED_ADMIN_BYPASS;
        log_reason = "GRANT_ADMIN_BYPASS";
    } else if (sdec == SCHED_ALLOW_SUPERVISOR_OVERRIDE) {
        final_res = ACCESS_RESULT_GRANTED_SUPERVISOR_EXIT;
        log_reason = "GRANT_SUPERVISOR_EXIT";
        logger_log_audit("SUPERVISOR", "OVERRIDE_EXIT_USED", user.uid);
    }
    s_stats.last_result = final_res;

    logger_log_access(user.uid, dir_str, log_reason, (int32_t)new_count);

    char line0[17];
    char line1[17];
    snprintf(line0, sizeof(line0), "GRANTED: %-6s", dir_str);
    snprintf(line1, sizeof(line1), "%-10.10s IN:%2lu", user.name, (unsigned long)new_count);
    display_show_alert(line0, line1, 2500);

    ESP_LOGI(TAG, "[%s] %s for %s (%s) -> Building Occupancy: %lu",
             dir_str, log_reason, user.uid, user.name, (unsigned long)new_count);
    return final_res;
}

static void access_task(void *arg)
{
    (void)arg;
    rfid_scan_event_t evt;
    while (1) {
        if (xQueueReceive(s_scan_queue, &evt, portMAX_DELAY) == pdTRUE) {
            process_scan_event(&evt);
        }
    }
}

esp_err_t access_control_init(void)
{
    if (s_scan_queue == NULL) {
        s_scan_queue = xQueueCreate(CFG_RFID_QUEUE_DEPTH, sizeof(rfid_scan_event_t));
        if (s_scan_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (xTaskCreate(access_task, "access_task", 6144, NULL, 6, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Main Entrance access control decision engine initialized");
    return ESP_OK;
}

void access_control_handle_scan(const rfid_scan_event_t *evt)
{
    if (evt != NULL && s_scan_queue != NULL) {
        xQueueSend(s_scan_queue, evt, pdMS_TO_TICKS(50));
    }
}

void access_control_handle_emergency_btn(void)
{
    bool new_mode = !schedule_is_emergency_mode();
    schedule_set_emergency_mode(new_mode);
    door_set_emergency_all(new_mode);
    net_protocol_broadcast_emergency(new_mode);

    logger_log_audit("HW_BUTTON", new_mode ? "EMERGENCY_ON" : "EMERGENCY_OFF",
                     new_mode ? "Emergency evacuation unlocked doors" : "Emergency mode cleared");
    ESP_LOGW(TAG, "Emergency Evacuation Mode toggled via hardware button -> %s",
             new_mode ? "ACTIVE (ALL DOORS UNLOCKED)" : "NORMAL");
}

access_result_t access_control_evaluate_sync(rfid_reader_t reader, const char *uid)
{
    rfid_scan_event_t evt = { 0 };
    evt.reader = reader;
    evt.from_hardware_btn = false;
    if (uid != NULL) {
        strncpy(evt.uid, uid, sizeof(evt.uid) - 1);
    }
    return process_scan_event(&evt);
}

esp_err_t access_control_get_stats(access_stats_t *out_stats)
{
    if (out_stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_stats = s_stats;
    return ESP_OK;
}

const char *access_control_result_to_str(access_result_t res)
{
    switch (res) {
        case ACCESS_RESULT_GRANTED_NORMAL:           return "GRANTED_NORMAL";
        case ACCESS_RESULT_GRANTED_ADMIN_BYPASS:     return "GRANTED_ADMIN_BYPASS";
        case ACCESS_RESULT_GRANTED_SUPERVISOR_EXIT:  return "GRANTED_SUPERVISOR_EXIT";
        case ACCESS_RESULT_GRANTED_EMERGENCY_EVAC:   return "GRANTED_EMERGENCY_EVAC";
        case ACCESS_RESULT_DENIED_UNKNOWN_TAG:       return "DENIED_UNKNOWN_TAG";
        case ACCESS_RESULT_DENIED_USER_INACTIVE:     return "DENIED_USER_INACTIVE";
        case ACCESS_RESULT_DENIED_VISITOR_EXPIRED:   return "DENIED_VISITOR_EXPIRED";
        case ACCESS_RESULT_DENIED_OUTSIDE_SCHED_ENTRY: return "DENIED_OUTSIDE_SCHED_ENTRY";
        case ACCESS_RESULT_DENIED_OUTSIDE_SCHED_EXIT:  return "DENIED_OUTSIDE_SCHED_EXIT";
        case ACCESS_RESULT_DENIED_EMERGENCY_LOCKDOWN:  return "DENIED_EMERGENCY_LOCKDOWN";
        case ACCESS_RESULT_DENIED_ANTIPASSBACK_ENTRY:  return "DENIED_ANTIPASSBACK_ENTRY";
        case ACCESS_RESULT_DENIED_ANTIPASSBACK_EXIT:   return "DENIED_ANTIPASSBACK_EXIT";
        default:                                     return "UNKNOWN";
    }
}
