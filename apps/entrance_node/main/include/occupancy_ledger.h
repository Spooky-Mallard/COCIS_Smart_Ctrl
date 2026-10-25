#ifndef OCCUPANCY_LEDGER_H
#define OCCUPANCY_LEDGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"
#include "esp_err.h"
#include "user_db.h"

typedef struct {
    bool        is_inside;
    char        uid[CFG_RFID_UID_MAX_LEN];
    char        name[CFG_USER_NAME_MAX_LEN];
    user_role_t role;
    int64_t     entry_epoch;
    int64_t     last_exit_epoch;
    uint32_t    visit_count;
} occupant_record_t;

typedef struct {
    uint32_t current_inside;
    uint32_t peak_inside;
    uint32_t total_entries;
    uint32_t total_exits;
    uint32_t antipassback_blocks;
    uint32_t recovered_on_boot;
} occupancy_stats_t;

esp_err_t occupancy_ledger_init(void);
bool      occupancy_ledger_is_inside(const char *uid);
esp_err_t occupancy_ledger_check_in(const user_record_t *user, int64_t now_epoch, uint32_t *out_new_count);
esp_err_t occupancy_ledger_check_out(const char *uid, int64_t now_epoch, uint32_t *out_new_count);
void      occupancy_ledger_record_apb_violation(void);

uint32_t  occupancy_ledger_get_count(void);
size_t    occupancy_ledger_list_inside(occupant_record_t *out_list, size_t max_items);
esp_err_t occupancy_ledger_get_stats(occupancy_stats_t *out_stats);
esp_err_t occupancy_ledger_clear_all(void);

#endif
