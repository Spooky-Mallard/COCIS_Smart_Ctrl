#ifndef ACCESS_CONTROL_H
#define ACCESS_CONTROL_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "rfid.h"

typedef enum {
    ACCESS_RESULT_GRANTED_NORMAL = 0,
    ACCESS_RESULT_GRANTED_ADMIN_BYPASS,
    ACCESS_RESULT_GRANTED_SUPERVISOR_EXIT,
    ACCESS_RESULT_GRANTED_EMERGENCY_EVAC,
    ACCESS_RESULT_DENIED_UNKNOWN_TAG,
    ACCESS_RESULT_DENIED_USER_INACTIVE,
    ACCESS_RESULT_DENIED_VISITOR_EXPIRED,
    ACCESS_RESULT_DENIED_OUTSIDE_SCHED_ENTRY,
    ACCESS_RESULT_DENIED_OUTSIDE_SCHED_EXIT,
    ACCESS_RESULT_DENIED_EMERGENCY_LOCKDOWN,
    ACCESS_RESULT_DENIED_ANTIPASSBACK_ENTRY,
    ACCESS_RESULT_DENIED_ANTIPASSBACK_EXIT
} access_result_t;

typedef struct {
    uint32_t granted_entry;
    uint32_t granted_exit;
    uint32_t denied_unknown;
    uint32_t denied_inactive;
    uint32_t denied_expired;
    uint32_t denied_schedule_entry;
    uint32_t denied_schedule_exit;
    uint32_t denied_antipassback;
    access_result_t last_result;
} access_stats_t;

esp_err_t       access_control_init(void);
void            access_control_handle_scan(const rfid_scan_event_t *evt);
void            access_control_handle_emergency_btn(void);
access_result_t access_control_evaluate_sync(rfid_reader_t reader, const char *uid);
esp_err_t       access_control_get_stats(access_stats_t *out_stats);
const char     *access_control_result_to_str(access_result_t res);

#endif
