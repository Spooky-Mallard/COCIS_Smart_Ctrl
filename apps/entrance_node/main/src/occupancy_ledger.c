#include "occupancy_ledger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "storage_sd.h"
#include "sync.h"

static const char *TAG = "OCC_LEDGER";

static occupant_record_t s_records[CFG_USER_DB_MAX_USERS];
static occupancy_stats_t s_stats = { 0 };

static occupant_record_t *find_or_alloc_slot_unlocked(const char *uid)
{
    occupant_record_t *free_slot = NULL;
    for (size_t i = 0; i < CFG_USER_DB_MAX_USERS; i++) {
        if (s_records[i].uid[0] != '\0' && strcasecmp(s_records[i].uid, uid) == 0) {
            return &s_records[i];
        }
        if (s_records[i].uid[0] == '\0' && free_slot == NULL) {
            free_slot = &s_records[i];
        }
    }
    return free_slot;
}

static void boot_recovery_visitor(const char *line, void *ctx)
{
    (void)ctx;
    /* Format: seq,datetime_eat,epoch,uid_or_actor,action,detail,value,sha256_chain */
    char buf[256];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    char *tok_seq = strtok_r(buf, ",", &saveptr);
    char *tok_dt  = strtok_r(NULL, ",", &saveptr);
    char *tok_ep  = strtok_r(NULL, ",", &saveptr);
    char *tok_uid = strtok_r(NULL, ",", &saveptr);
    char *tok_act = strtok_r(NULL, ",", &saveptr);
    char *tok_det = strtok_r(NULL, ",", &saveptr);
    (void)tok_seq;
    (void)tok_dt;

    if (!tok_ep || !tok_uid || !tok_act || !tok_det) {
        return;
    }
    if (strncmp(tok_det, "GRANT", 5) != 0 && strncmp(tok_det, "ALLOW", 5) != 0) {
        return;
    }

    int64_t ep = atoll(tok_ep);
    user_record_t u;
    if (user_db_find(tok_uid, &u) != ESP_OK) {
        memset(&u, 0, sizeof(u));
        strncpy(u.uid, tok_uid, sizeof(u.uid) - 1);
        strncpy(u.name, "Recovered User", sizeof(u.name) - 1);
        u.role = USER_ROLE_STUDENT;
    }

    occupant_record_t *slot = find_or_alloc_slot_unlocked(tok_uid);
    if (slot == NULL) {
        return;
    }

    strncpy(slot->uid, u.uid, sizeof(slot->uid) - 1);
    strncpy(slot->name, u.name, sizeof(slot->name) - 1);
    slot->role = u.role;

    if (strcmp(tok_act, "ENTRY") == 0 && !slot->is_inside) {
        slot->is_inside = true;
        slot->entry_epoch = ep;
        slot->visit_count++;
        s_stats.current_inside++;
        if (s_stats.current_inside > s_stats.peak_inside) {
            s_stats.peak_inside = s_stats.current_inside;
        }
    } else if (strcmp(tok_act, "EXIT") == 0 && slot->is_inside) {
        slot->is_inside = false;
        slot->last_exit_epoch = ep;
        if (s_stats.current_inside > 0) {
            s_stats.current_inside--;
        }
    }
}

esp_err_t occupancy_ledger_init(void)
{
    esp_err_t err = sync_lock(LOCK_LEVEL_LEDGER, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    memset(s_records, 0, sizeof(s_records));
    memset(&s_stats, 0, sizeof(s_stats));
    sync_unlock(LOCK_LEVEL_LEDGER);

    if (storage_sd_is_mounted()) {
        if (storage_sd_read_lines("access.csv", boot_recovery_visitor, NULL) == ESP_OK) {
            s_stats.recovered_on_boot = s_stats.current_inside;
            if (s_stats.recovered_on_boot > 0) {
                ESP_LOGI(TAG, "Recovered %lu occupant(s) inside CoCIS from SD access.csv",
                         (unsigned long)s_stats.recovered_on_boot);
            }
        }
    }

    ESP_LOGI(TAG, "Occupancy ledger initialized (currently inside: %lu)", (unsigned long)s_stats.current_inside);
    return ESP_OK;
}

bool occupancy_ledger_is_inside(const char *uid)
{
    if (uid == NULL || uid[0] == '\0') {
        return false;
    }

    if (sync_lock(LOCK_LEVEL_LEDGER, CFG_LOCK_TIMEOUT_MS) != ESP_OK) {
        return false;
    }

    bool inside = false;
    for (size_t i = 0; i < CFG_USER_DB_MAX_USERS; i++) {
        if (s_records[i].uid[0] != '\0' && strcasecmp(s_records[i].uid, uid) == 0) {
            inside = s_records[i].is_inside;
            break;
        }
    }

    sync_unlock(LOCK_LEVEL_LEDGER);
    return inside;
}

esp_err_t occupancy_ledger_check_in(const user_record_t *user, int64_t now_epoch, uint32_t *out_new_count)
{
    if (user == NULL || user->uid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_LEDGER, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    occupant_record_t *slot = find_or_alloc_slot_unlocked(user->uid);
    if (slot == NULL) {
        sync_unlock(LOCK_LEVEL_LEDGER);
        return ESP_ERR_NO_MEM;
    }

    if (slot->is_inside) {
        s_stats.antipassback_blocks++;
        sync_unlock(LOCK_LEVEL_LEDGER);
        return ESP_ERR_INVALID_STATE;
    }

    strncpy(slot->uid, user->uid, sizeof(slot->uid) - 1);
    strncpy(slot->name, user->name, sizeof(slot->name) - 1);
    slot->role = user->role;
    slot->is_inside = true;
    slot->entry_epoch = now_epoch;
    slot->visit_count++;

    s_stats.current_inside++;
    s_stats.total_entries++;
    if (s_stats.current_inside > s_stats.peak_inside) {
        s_stats.peak_inside = s_stats.current_inside;
    }

    if (out_new_count != NULL) {
        *out_new_count = s_stats.current_inside;
    }

    sync_unlock(LOCK_LEVEL_LEDGER);
    return ESP_OK;
}

esp_err_t occupancy_ledger_check_out(const char *uid, int64_t now_epoch, uint32_t *out_new_count)
{
    if (uid == NULL || uid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_LEDGER, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    occupant_record_t *slot = find_or_alloc_slot_unlocked(uid);
    if (slot == NULL || !slot->is_inside) {
        s_stats.antipassback_blocks++;
        sync_unlock(LOCK_LEVEL_LEDGER);
        return ESP_ERR_INVALID_STATE;
    }

    slot->is_inside = false;
    slot->last_exit_epoch = now_epoch;
    if (s_stats.current_inside > 0) {
        s_stats.current_inside--;
    }
    s_stats.total_exits++;

    if (out_new_count != NULL) {
        *out_new_count = s_stats.current_inside;
    }

    sync_unlock(LOCK_LEVEL_LEDGER);
    return ESP_OK;
}

void occupancy_ledger_record_apb_violation(void)
{
    if (sync_lock(LOCK_LEVEL_LEDGER, CFG_LOCK_TIMEOUT_MS) == ESP_OK) {
        s_stats.antipassback_blocks++;
        sync_unlock(LOCK_LEVEL_LEDGER);
    }
}

uint32_t occupancy_ledger_get_count(void)
{
    uint32_t cnt = 0;
    if (sync_lock(LOCK_LEVEL_LEDGER, CFG_LOCK_TIMEOUT_MS) == ESP_OK) {
        cnt = s_stats.current_inside;
        sync_unlock(LOCK_LEVEL_LEDGER);
    }
    return cnt;
}

size_t occupancy_ledger_list_inside(occupant_record_t *out_list, size_t max_items)
{
    if (out_list == NULL || max_items == 0) {
        return 0;
    }

    if (sync_lock(LOCK_LEVEL_LEDGER, CFG_LOCK_TIMEOUT_MS) != ESP_OK) {
        return 0;
    }

    size_t n = 0;
    for (size_t i = 0; i < CFG_USER_DB_MAX_USERS && n < max_items; i++) {
        if (s_records[i].uid[0] != '\0' && s_records[i].is_inside) {
            out_list[n++] = s_records[i];
        }
    }

    sync_unlock(LOCK_LEVEL_LEDGER);
    return n;
}

esp_err_t occupancy_ledger_get_stats(occupancy_stats_t *out_stats)
{
    if (out_stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_LEDGER, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    *out_stats = s_stats;
    sync_unlock(LOCK_LEVEL_LEDGER);
    return ESP_OK;
}

esp_err_t occupancy_ledger_clear_all(void)
{
    esp_err_t err = sync_lock(LOCK_LEVEL_LEDGER, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    memset(s_records, 0, sizeof(s_records));
    memset(&s_stats, 0, sizeof(s_stats));
    sync_unlock(LOCK_LEVEL_LEDGER);
    return ESP_OK;
}
