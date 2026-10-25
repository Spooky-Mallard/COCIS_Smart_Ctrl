#ifndef AUTH_H
#define AUTH_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool     logged_in;
    char     username[16];
    uint32_t remaining_session_sec;
    uint32_t failed_attempts;
    uint32_t lockout_remaining_sec;
} auth_status_t;

esp_err_t auth_init(void);
esp_err_t auth_login(const char *username, const char *password, const char *opt_admin_uid);
void      auth_logout(void);
bool      auth_is_logged_in(void);
void      auth_touch_activity(void);
esp_err_t auth_change_password(const char *old_password, const char *new_password);
esp_err_t auth_get_status(auth_status_t *out_status);
esp_err_t auth_reset_lockout(void);

#endif
