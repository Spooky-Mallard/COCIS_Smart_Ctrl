#include "schedule.h"
#include <ctype.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "sync.h"
#include "time_sync.h"

static const char *TAG = "SCHEDULE";
static const char *NVS_KEY_SCHED = "sched_v1";

static weekly_schedule_t s_schedule;
static bool              s_emergency_mode = false;
static char              s_override_uid[CFG_RFID_UID_MAX_LEN] = { 0 };
static int64_t           s_override_expiry_epoch = 0;

static void load_default_schedule_unlocked(void)
{
    /* Sunday (0): Closed by default */
    s_schedule.days[0] = (day_schedule_t){ .enabled = false, .open_hour = 0, .open_min = 0, .close_hour = 0, .close_min = 0 };

    /* Monday (1) through Friday (5): 07:00 - 21:00 EAT */
    for (int d = 1; d <= 5; d++) {
        s_schedule.days[d] = (day_schedule_t){ .enabled = true, .open_hour = 7, .open_min = 0, .close_hour = 21, .close_min = 0 };
    }

    /* Saturday (6): 08:00 - 17:00 EAT */
    s_schedule.days[6] = (day_schedule_t){ .enabled = true, .open_hour = 8, .open_min = 0, .close_hour = 17, .close_min = 0 };
}

static void save_schedule_nvs_unlocked(void)
{
    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_blob(h, NVS_KEY_SCHED, &s_schedule, sizeof(s_schedule)) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
}

esp_err_t schedule_init(void)
{
    esp_err_t err = sync_lock(LOCK_LEVEL_SCHEDULE, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    bool loaded = false;
    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_schedule);
        if (nvs_get_blob(h, NVS_KEY_SCHED, &s_schedule, &sz) == ESP_OK && sz == sizeof(s_schedule)) {
            loaded = true;
        }
        nvs_close(h);
    }

    if (!loaded) {
        load_default_schedule_unlocked();
        save_schedule_nvs_unlocked();
        ESP_LOGI(TAG, "Initialized default CoCIS weekly access schedule");
    } else {
        ESP_LOGI(TAG, "Loaded weekly access schedule from NVS");
    }

    sync_unlock(LOCK_LEVEL_SCHEDULE);
    return ESP_OK;
}

esp_err_t schedule_get_weekly(weekly_schedule_t *out_sched)
{
    if (out_sched == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_SCHEDULE, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    *out_sched = s_schedule;
    sync_unlock(LOCK_LEVEL_SCHEDULE);
    return ESP_OK;
}

esp_err_t schedule_set_day(int day_of_week, bool enabled, uint8_t open_h, uint8_t open_m, uint8_t close_h, uint8_t close_m)
{
    if (day_of_week < 0 || day_of_week > 6 || open_h > 23 || open_m > 59 || close_h > 23 || close_m > 59) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t open_total = (uint16_t)(open_h * 60 + open_m);
    uint16_t close_total = (uint16_t)(close_h * 60 + close_m);
    if (enabled && close_total <= open_total) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_SCHEDULE, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    s_schedule.days[day_of_week].enabled = enabled;
    s_schedule.days[day_of_week].open_hour = open_h;
    s_schedule.days[day_of_week].open_min = open_m;
    s_schedule.days[day_of_week].close_hour = close_h;
    s_schedule.days[day_of_week].close_min = close_m;

    save_schedule_nvs_unlocked();
    sync_unlock(LOCK_LEVEL_SCHEDULE);
    return ESP_OK;
}

esp_err_t schedule_reset_defaults(void)
{
    esp_err_t err = sync_lock(LOCK_LEVEL_SCHEDULE, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    load_default_schedule_unlocked();
    save_schedule_nvs_unlocked();
    sync_unlock(LOCK_LEVEL_SCHEDULE);
    return ESP_OK;
}

schedule_decision_t schedule_evaluate(bool is_exit, user_role_t role, const char *uid)
{
    time_snapshot_t now;
    time_sync_get_now(&now);

    if (sync_lock(LOCK_LEVEL_SCHEDULE, CFG_LOCK_TIMEOUT_MS) != ESP_OK) {
        return SCHED_DENY_TIME_INVALID;
    }

    if (s_emergency_mode) {
        sync_unlock(LOCK_LEVEL_SCHEDULE);
        if (is_exit) {
            return SCHED_ALLOW_EMERGENCY_EXIT;
        }
        return (role == USER_ROLE_ADMIN) ? SCHED_ALLOW_ADMIN_BYPASS : SCHED_DENY_EMERGENCY_LOCKDOWN_ENTRY;
    }

    if (!now.time_valid || now.day_of_week < 0 || now.day_of_week > 6) {
        sync_unlock(LOCK_LEVEL_SCHEDULE);
        return is_exit ? SCHED_ALLOW_NORMAL : SCHED_DENY_TIME_INVALID;
    }

    const day_schedule_t *day = &s_schedule.days[now.day_of_week];
    int cur_mins = now.hour * 60 + now.minute;
    int open_mins = day->open_hour * 60 + day->open_min;
    int close_mins = day->close_hour * 60 + day->close_min;
    bool in_window = day->enabled && (cur_mins >= open_mins) && (cur_mins < close_mins);

    if (in_window) {
        sync_unlock(LOCK_LEVEL_SCHEDULE);
        return SCHED_ALLOW_NORMAL;
    }

    /* Outside authorized schedule window */
    if (role == USER_ROLE_ADMIN) {
        sync_unlock(LOCK_LEVEL_SCHEDULE);
        return SCHED_ALLOW_ADMIN_BYPASS;
    }

    if (is_exit && uid != NULL && s_override_uid[0] != '\0' &&
        strcasecmp(uid, s_override_uid) == 0 && now.epoch <= s_override_expiry_epoch) {
        /* Consume the one-time supervisor safe-exit pass */
        s_override_uid[0] = '\0';
        s_override_expiry_epoch = 0;
        sync_unlock(LOCK_LEVEL_SCHEDULE);
        return SCHED_ALLOW_SUPERVISOR_OVERRIDE;
    }

    bool day_enabled = day->enabled;
    sync_unlock(LOCK_LEVEL_SCHEDULE);

    if (!is_exit && !day_enabled) {
        return SCHED_DENY_DAY_CLOSED;
    }
    return is_exit ? SCHED_DENY_OUTSIDE_HOURS_EXIT : SCHED_DENY_OUTSIDE_HOURS_ENTRY;
}

bool schedule_is_open_now(void)
{
    time_snapshot_t now;
    time_sync_get_now(&now);
    if (!now.time_valid || now.day_of_week < 0 || now.day_of_week > 6) {
        return false;
    }

    if (sync_lock(LOCK_LEVEL_SCHEDULE, CFG_LOCK_TIMEOUT_MS) != ESP_OK) {
        return false;
    }

    const day_schedule_t *day = &s_schedule.days[now.day_of_week];
    int cur_mins = now.hour * 60 + now.minute;
    int open_mins = day->open_hour * 60 + day->open_min;
    int close_mins = day->close_hour * 60 + day->close_min;
    bool open = day->enabled && (cur_mins >= open_mins) && (cur_mins < close_mins);

    sync_unlock(LOCK_LEVEL_SCHEDULE);
    return open;
}

esp_err_t schedule_grant_exit_override(const char *uid)
{
    if (uid == NULL || uid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    time_snapshot_t now;
    time_sync_get_now(&now);

    esp_err_t err = sync_lock(LOCK_LEVEL_SCHEDULE, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    size_t i = 0;
    while (uid[i] != '\0' && i + 1 < sizeof(s_override_uid)) {
        s_override_uid[i] = (char)toupper((unsigned char)uid[i]);
        i++;
    }
    s_override_uid[i] = '\0';
    s_override_expiry_epoch = now.epoch + CFG_SUPERVISOR_OVERRIDE_TTL_SEC;

    sync_unlock(LOCK_LEVEL_SCHEDULE);
    ESP_LOGW(TAG, "Granted 5-min supervisor exit override to %s", s_override_uid);
    return ESP_OK;
}

bool schedule_has_exit_override(const char *uid, int64_t *out_expiry_epoch)
{
    time_snapshot_t now;
    time_sync_get_now(&now);

    if (sync_lock(LOCK_LEVEL_SCHEDULE, CFG_LOCK_TIMEOUT_MS) != ESP_OK) {
        return false;
    }

    bool active = (s_override_uid[0] != '\0' && now.epoch <= s_override_expiry_epoch);
    if (active && uid != NULL && uid[0] != '\0') {
        active = (strcasecmp(uid, s_override_uid) == 0);
    }
    if (active && out_expiry_epoch != NULL) {
        *out_expiry_epoch = s_override_expiry_epoch;
    }

    sync_unlock(LOCK_LEVEL_SCHEDULE);
    return active;
}

esp_err_t schedule_set_emergency_mode(bool active)
{
    esp_err_t err = sync_lock(LOCK_LEVEL_SCHEDULE, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    s_emergency_mode = active;
    if (active) {
        sync_event_set(SYS_EVT_EMERGENCY_MODE);
    } else {
        sync_event_clear(SYS_EVT_EMERGENCY_MODE);
    }

    sync_unlock(LOCK_LEVEL_SCHEDULE);
    return ESP_OK;
}

bool schedule_is_emergency_mode(void)
{
    return sync_event_is_set(SYS_EVT_EMERGENCY_MODE);
}

int schedule_parse_day_name(const char *day_str)
{
    if (day_str == NULL) {
        return -1;
    }
    static const char *const names[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
    for (int i = 0; i < 7; i++) {
        if (strncasecmp(day_str, names[i], 3) == 0) {
            return i;
        }
    }
    return -1;
}

const char *schedule_day_name(int day_of_week)
{
    static const char *const names[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    if (day_of_week < 0 || day_of_week > 6) {
        return "INVALID";
    }
    return names[day_of_week];
}

const char *schedule_decision_to_str(schedule_decision_t dec)
{
    switch (dec) {
        case SCHED_ALLOW_NORMAL:                  return "ALLOW_SCHEDULE_OPEN";
        case SCHED_ALLOW_ADMIN_BYPASS:            return "ALLOW_ADMIN_BYPASS";
        case SCHED_ALLOW_SUPERVISOR_OVERRIDE:     return "ALLOW_SUPERVISOR_EXIT";
        case SCHED_ALLOW_EMERGENCY_EXIT:          return "ALLOW_EMERGENCY_EVAC";
        case SCHED_DENY_TIME_INVALID:             return "DENY_CLOCK_INVALID";
        case SCHED_DENY_DAY_CLOSED:               return "DENY_DAY_CLOSED";
        case SCHED_DENY_OUTSIDE_HOURS_ENTRY:      return "DENY_AFTER_HRS_ENTRY";
        case SCHED_DENY_OUTSIDE_HOURS_EXIT:       return "DENY_AFTER_HRS_EXIT";
        case SCHED_DENY_EMERGENCY_LOCKDOWN_ENTRY: return "DENY_EMERGENCY_ENTRY";
        default:                                  return "DENY_SCHEDULE";
    }
}
