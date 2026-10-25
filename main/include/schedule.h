#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "esp_err.h"
#include "user_db.h"

typedef struct {
    bool    enabled;
    uint8_t open_hour;
    uint8_t open_min;
    uint8_t close_hour;
    uint8_t close_min;
} day_schedule_t;

typedef struct {
    day_schedule_t days[7]; /* 0 = Sun, 1 = Mon .. 6 = Sat */
} weekly_schedule_t;

typedef enum {
    SCHED_ALLOW_NORMAL = 0,
    SCHED_ALLOW_ADMIN_BYPASS,
    SCHED_ALLOW_SUPERVISOR_OVERRIDE,
    SCHED_ALLOW_EMERGENCY_EXIT,
    SCHED_DENY_TIME_INVALID,
    SCHED_DENY_DAY_CLOSED,
    SCHED_DENY_OUTSIDE_HOURS_ENTRY,
    SCHED_DENY_OUTSIDE_HOURS_EXIT,
    SCHED_DENY_EMERGENCY_LOCKDOWN_ENTRY
} schedule_decision_t;

esp_err_t           schedule_init(void);
esp_err_t           schedule_get_weekly(weekly_schedule_t *out_sched);
esp_err_t           schedule_set_day(int day_of_week, bool enabled, uint8_t open_h, uint8_t open_m, uint8_t close_h, uint8_t close_m);
esp_err_t           schedule_reset_defaults(void);

schedule_decision_t schedule_evaluate(bool is_exit, user_role_t role, const char *uid);
bool                schedule_is_open_now(void);

esp_err_t           schedule_grant_exit_override(const char *uid);
bool                schedule_has_exit_override(const char *uid, int64_t *out_expiry_epoch);

esp_err_t           schedule_set_emergency_mode(bool active);
bool                schedule_is_emergency_mode(void);

int                 schedule_parse_day_name(const char *day_str);
const char         *schedule_day_name(int day_of_week);
const char         *schedule_decision_to_str(schedule_decision_t dec);

#endif
