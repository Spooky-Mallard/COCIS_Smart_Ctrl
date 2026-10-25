#include "auth.h"
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "logger.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "sync.h"
#include "user_db.h"

static const char *TAG = "AUTH";
static const char *NVS_KEY_CRED = "auth_cred_v1";

typedef struct {
    char    username[CFG_AUTH_USER_MAX_LEN];
    uint8_t salt[CFG_AUTH_SALT_LEN];
    uint8_t sha256_hash[CFG_AUTH_HASH_LEN];
} stored_cred_t;

static stored_cred_t s_cred;
static bool          s_logged_in = false;
static int64_t       s_last_activity_sec = 0;
static uint32_t      s_failed_attempts = 0;
static int64_t       s_lockout_until_sec = 0;

static int64_t uptime_sec(void)
{
    return esp_timer_get_time() / 1000000LL;
}

static bool compute_salted_sha256(const uint8_t *salt, size_t salt_len, const char *password, uint8_t *out_hash32)
{
    uint8_t buf[CFG_AUTH_SALT_LEN + CFG_AUTH_PASS_MAX_LEN + 1];
    size_t pass_len = strlen(password);
    if (salt_len > CFG_AUTH_SALT_LEN || pass_len > CFG_AUTH_PASS_MAX_LEN) {
        return false;
    }

    memcpy(buf, salt, salt_len);
    memcpy(buf + salt_len, password, pass_len);

    size_t out_len = 0;
    psa_status_t st = psa_hash_compute(PSA_ALG_SHA_256, buf, salt_len + pass_len, out_hash32, 32, &out_len);
    memset(buf, 0, sizeof(buf));
    return (st == PSA_SUCCESS && out_len == 32);
}

static bool constant_time_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return (diff == 0);
}

static void save_cred_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_blob(h, NVS_KEY_CRED, &s_cred, sizeof(s_cred)) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
}

esp_err_t auth_init(void)
{
    psa_crypto_init();

    bool loaded = false;
    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_cred);
        if (nvs_get_blob(h, NVS_KEY_CRED, &s_cred, &sz) == ESP_OK && sz == sizeof(s_cred)) {
            loaded = true;
        }
        nvs_close(h);
    }

    if (!loaded) {
        memset(&s_cred, 0, sizeof(s_cred));
        strncpy(s_cred.username, CFG_AUTH_DEFAULT_USER, sizeof(s_cred.username) - 1);
        esp_fill_random(s_cred.salt, sizeof(s_cred.salt));
        compute_salted_sha256(s_cred.salt, sizeof(s_cred.salt), CFG_AUTH_DEFAULT_PASS, s_cred.sha256_hash);
        save_cred_nvs();
        ESP_LOGI(TAG, "Provisioned salted SHA-256 admin credentials for '%s'", s_cred.username);
    } else {
        ESP_LOGI(TAG, "Loaded salted SHA-256 admin credentials from NVS");
    }

    s_logged_in = false;
    sync_event_clear(SYS_EVT_ADMIN_AUTH);
    return ESP_OK;
}

esp_err_t auth_login(const char *username, const char *password, const char *opt_admin_uid)
{
    if (username == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    int64_t now_s = uptime_sec();
    if (now_s < s_lockout_until_sec) {
        logger_log_audit(username, "LOGIN_LOCKED", "Attempt during active lockout");
        return ESP_ERR_INVALID_STATE;
    }

    sys_config_t cfg;
    config_get(&cfg);
    if (cfg.require_admin_2fa) {
        user_record_t u;
        if (opt_admin_uid == NULL || user_db_find(opt_admin_uid, &u) != ESP_OK ||
            !u.active || u.role != USER_ROLE_ADMIN) {
            logger_log_audit(username, "LOGIN_2FA_FAIL", "Missing or invalid Admin 2FA RFID UID");
            return ESP_ERR_NOT_ALLOWED;
        }
    }

    uint8_t candidate_hash[CFG_AUTH_HASH_LEN] = { 0 };
    bool hash_ok = compute_salted_sha256(s_cred.salt, sizeof(s_cred.salt), password, candidate_hash);
    bool user_ok = (strcmp(username, s_cred.username) == 0);
    bool pass_ok = hash_ok && constant_time_equal(candidate_hash, s_cred.sha256_hash, CFG_AUTH_HASH_LEN);
    memset(candidate_hash, 0, sizeof(candidate_hash));

    if (!user_ok || !pass_ok) {
        s_failed_attempts++;
        if (s_failed_attempts >= CFG_AUTH_MAX_FAILED_ATTEMPTS) {
            uint32_t shift = s_failed_attempts - CFG_AUTH_MAX_FAILED_ATTEMPTS;
            if (shift > 4) {
                shift = 4;
            }
            uint32_t lock_sec = CFG_AUTH_LOCKOUT_BASE_SEC * (1U << shift);
            s_lockout_until_sec = now_s + lock_sec;
            char detail[48];
            snprintf(detail, sizeof(detail), "Locked out for %lu s (fails=%lu)",
                     (unsigned long)lock_sec, (unsigned long)s_failed_attempts);
            logger_log_audit(username, "AUTH_LOCKOUT", detail);
            ESP_LOGW(TAG, "%s", detail);
        } else {
            logger_log_audit(username, "LOGIN_FAIL", "Invalid username or password");
        }
        return ESP_FAIL;
    }

    s_failed_attempts = 0;
    s_lockout_until_sec = 0;
    s_logged_in = true;
    s_last_activity_sec = now_s;
    sync_event_set(SYS_EVT_ADMIN_AUTH);

    logger_log_audit(username, "LOGIN_SUCCESS", "Admin CLI session opened");
    return ESP_OK;
}

void auth_logout(void)
{
    if (s_logged_in) {
        s_logged_in = false;
        sync_event_clear(SYS_EVT_ADMIN_AUTH);
        logger_log_audit(s_cred.username, "LOGOUT", "Admin session closed");
    }
}

bool auth_is_logged_in(void)
{
    if (!s_logged_in) {
        return false;
    }

    sys_config_t cfg;
    config_get(&cfg);
    int64_t now_s = uptime_sec();
    if ((now_s - s_last_activity_sec) >= (int64_t)cfg.auth_timeout_sec) {
        s_logged_in = false;
        sync_event_clear(SYS_EVT_ADMIN_AUTH);
        logger_log_audit(s_cred.username, "AUTO_LOGOUT", "Session expired due to inactivity");
        return false;
    }
    return true;
}

void auth_touch_activity(void)
{
    if (s_logged_in) {
        s_last_activity_sec = uptime_sec();
    }
}

esp_err_t auth_change_password(const char *old_password, const char *new_password)
{
    if (!auth_is_logged_in()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (old_password == NULL || new_password == NULL || strlen(new_password) < 6) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t old_hash[CFG_AUTH_HASH_LEN] = { 0 };
    if (!compute_salted_sha256(s_cred.salt, sizeof(s_cred.salt), old_password, old_hash) ||
        !constant_time_equal(old_hash, s_cred.sha256_hash, CFG_AUTH_HASH_LEN)) {
        logger_log_audit(s_cred.username, "PASSWD_FAIL", "Old password mismatch");
        return ESP_FAIL;
    }

    esp_fill_random(s_cred.salt, sizeof(s_cred.salt));
    compute_salted_sha256(s_cred.salt, sizeof(s_cred.salt), new_password, s_cred.sha256_hash);
    save_cred_nvs();
    auth_touch_activity();

    logger_log_audit(s_cred.username, "PASSWD_CHANGED", "Salted SHA-256 credentials updated");
    return ESP_OK;
}

esp_err_t auth_get_status(auth_status_t *out_status)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    bool active = auth_is_logged_in();
    int64_t now_s = uptime_sec();
    sys_config_t cfg;
    config_get(&cfg);

    out_status->logged_in = active;
    strncpy(out_status->username, s_cred.username, sizeof(out_status->username) - 1);
    out_status->username[sizeof(out_status->username) - 1] = '\0';

    if (active) {
        int64_t elapsed = now_s - s_last_activity_sec;
        out_status->remaining_session_sec = (elapsed < (int64_t)cfg.auth_timeout_sec)
                                            ? (uint32_t)((int64_t)cfg.auth_timeout_sec - elapsed) : 0;
    } else {
        out_status->remaining_session_sec = 0;
    }

    out_status->failed_attempts = s_failed_attempts;
    out_status->lockout_remaining_sec = (now_s < s_lockout_until_sec)
                                        ? (uint32_t)(s_lockout_until_sec - now_s) : 0;
    return ESP_OK;
}

esp_err_t auth_reset_lockout(void)
{
    s_failed_attempts = 0;
    s_lockout_until_sec = 0;
    return ESP_OK;
}
