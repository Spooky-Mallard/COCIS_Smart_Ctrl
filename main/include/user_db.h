#ifndef USER_DB_H
#define USER_DB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "config.h"
#include "esp_err.h"

typedef enum {
    USER_ROLE_ADMIN = 0,
    USER_ROLE_STAFF = 1,
    USER_ROLE_STUDENT = 2,
    USER_ROLE_VISITOR = 3
} user_role_t;

typedef struct {
    bool        in_use;
    char        uid[CFG_RFID_UID_MAX_LEN];
    char        name[CFG_USER_NAME_MAX_LEN];
    user_role_t role;
    bool        active;
    int64_t     expiry_epoch; /* 0 = never expires; >0 = visitor expiry */
} user_record_t;

esp_err_t   user_db_init(void);
esp_err_t   user_db_find(const char *uid, user_record_t *out_user);
esp_err_t   user_db_add(const char *uid, const char *name, user_role_t role, bool active, int64_t expiry_epoch);
esp_err_t   user_db_remove(const char *uid);
esp_err_t   user_db_set_active(const char *uid, bool active);
size_t      user_db_list(user_record_t *out_array, size_t max_items);
esp_err_t   user_db_reset_defaults(void);
const char *user_db_role_to_str(user_role_t role);
bool        user_db_parse_role(const char *str, user_role_t *out_role);

#endif
