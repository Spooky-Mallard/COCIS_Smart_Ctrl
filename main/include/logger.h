#ifndef LOGGER_H
#define LOGGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum {
    LOG_CAT_ACCESS = 0,
    LOG_CAT_LIGHTING = 1,
    LOG_CAT_AUDIT = 2
} log_category_t;

typedef struct {
    uint32_t       seq_id;
    int64_t        timestamp;
    log_category_t category;
    char           tag_or_actor[16];
    char           action[24];
    char           detail[48];
    int32_t        value;
    char           chain_hash[9]; /* 8-hex-char SHA-256 chain prefix + NUL */
} log_entry_t;

esp_err_t logger_init(void);
esp_err_t logger_log_access(const char *uid, const char *direction, const char *result, int32_t inside_count);
esp_err_t logger_log_lighting(uint8_t room_id, const char *state_str, int32_t energy_saved_mwh);
esp_err_t logger_log_audit(const char *actor, const char *action, const char *detail);

size_t    logger_get_recent(log_entry_t *out_entries, size_t max_entries, const char *filter_uid);
void      logger_get_chain_tip(char *out_hash, size_t buf_len);
esp_err_t logger_verify_integrity(uint32_t *out_checked, bool *out_valid);
esp_err_t logger_reset_chain(void);
void      logger_flush_sync(uint32_t wait_ms);

#endif
