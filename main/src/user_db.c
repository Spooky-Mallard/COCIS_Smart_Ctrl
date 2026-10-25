#include "user_db.h"
#include <ctype.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "sync.h"

static const char *TAG = "USER_DB";
static const char *NVS_KEY_USERS = "users_v1";

static user_record_t s_users[CFG_USER_DB_MAX_USERS];

static void normalize_uid(const char *in, char *out, size_t out_len)
{
    size_t i = 0;
    while (in[i] != '\0' && i + 1 < out_len) {
        out[i] = (char)toupper((unsigned char)in[i]);
        i++;
    }
    out[i] = '\0';
}

static void save_to_nvs_unlocked(void)
{
    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        if (nvs_set_blob(h, NVS_KEY_USERS, s_users, sizeof(s_users)) == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
}

static void seed_default_users_unlocked(void)
{
    memset(s_users, 0, sizeof(s_users));

    static const struct {
        const char *uid;
        const char *name;
        user_role_t role;
        bool        active;
        int64_t     expiry;
    } defaults[] = {
        { "04A00001", "Prof. Tonny Oyana",      USER_ROLE_ADMIN,   true,  0 },
        { "04A1B2C3", "Matthew Mumbere",        USER_ROLE_STUDENT, true,  0 },
        { "04B2C3D4", "Dr. Eng. Bainomugisha",  USER_ROLE_STAFF,   true,  0 },
        { "04C3D4E5", "Sarah Namukasa",         USER_ROLE_STUDENT, true,  0 },
        { "04D4E5F6", "David Okello",           USER_ROLE_STUDENT, true,  0 },
        { "04E5F607", "Grace Atim",             USER_ROLE_STAFF,   true,  0 },
        { "04V10008", "External Examiner",      USER_ROLE_VISITOR, true,  CFG_DEFAULT_BOOT_EPOCH + CFG_VISITOR_DEFAULT_TTL_SEC },
        { "0499DEAD", "Suspended Account",      USER_ROLE_STUDENT, false, 0 }
    };

    for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
        s_users[i].in_use = true;
        strncpy(s_users[i].uid, defaults[i].uid, sizeof(s_users[i].uid) - 1);
        strncpy(s_users[i].name, defaults[i].name, sizeof(s_users[i].name) - 1);
        s_users[i].role = defaults[i].role;
        s_users[i].active = defaults[i].active;
        s_users[i].expiry_epoch = defaults[i].expiry;
    }
}

esp_err_t user_db_init(void)
{
    esp_err_t err = sync_lock(LOCK_LEVEL_USER_DB, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    bool loaded = false;
    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_users);
        if (nvs_get_blob(h, NVS_KEY_USERS, s_users, &sz) == ESP_OK && sz == sizeof(s_users)) {
            loaded = true;
        }
        nvs_close(h);
    }

    if (!loaded) {
        seed_default_users_unlocked();
        save_to_nvs_unlocked();
        ESP_LOGI(TAG, "Seeded 8 default CoCIS RFID user records");
    } else {
        ESP_LOGI(TAG, "Loaded enrolled RFID user database from NVS");
    }

    sync_unlock(LOCK_LEVEL_USER_DB);
    return ESP_OK;
}

esp_err_t user_db_find(const char *uid, user_record_t *out_user)
{
    if (uid == NULL || uid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char norm[CFG_RFID_UID_MAX_LEN];
    normalize_uid(uid, norm, sizeof(norm));

    esp_err_t err = sync_lock(LOCK_LEVEL_USER_DB, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    esp_err_t res = ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < CFG_USER_DB_MAX_USERS; i++) {
        if (s_users[i].in_use && strcmp(s_users[i].uid, norm) == 0) {
            if (out_user != NULL) {
                *out_user = s_users[i];
            }
            res = ESP_OK;
            break;
        }
    }

    sync_unlock(LOCK_LEVEL_USER_DB);
    return res;
}

esp_err_t user_db_add(const char *uid, const char *name, user_role_t role, bool active, int64_t expiry_epoch)
{
    if (uid == NULL || uid[0] == '\0' || name == NULL || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char norm[CFG_RFID_UID_MAX_LEN];
    normalize_uid(uid, norm, sizeof(norm));

    esp_err_t err = sync_lock(LOCK_LEVEL_USER_DB, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    int free_slot = -1;
    for (int i = 0; i < CFG_USER_DB_MAX_USERS; i++) {
        if (s_users[i].in_use && strcmp(s_users[i].uid, norm) == 0) {
            strncpy(s_users[i].name, name, sizeof(s_users[i].name) - 1);
            s_users[i].name[sizeof(s_users[i].name) - 1] = '\0';
            s_users[i].role = role;
            s_users[i].active = active;
            s_users[i].expiry_epoch = expiry_epoch;
            save_to_nvs_unlocked();
            sync_unlock(LOCK_LEVEL_USER_DB);
            return ESP_OK;
        }
        if (!s_users[i].in_use && free_slot < 0) {
            free_slot = i;
        }
    }

    if (free_slot < 0) {
        sync_unlock(LOCK_LEVEL_USER_DB);
        return ESP_ERR_NO_MEM;
    }

    memset(&s_users[free_slot], 0, sizeof(user_record_t));
    s_users[free_slot].in_use = true;
    strncpy(s_users[free_slot].uid, norm, sizeof(s_users[free_slot].uid) - 1);
    strncpy(s_users[free_slot].name, name, sizeof(s_users[free_slot].name) - 1);
    s_users[free_slot].role = role;
    s_users[free_slot].active = active;
    s_users[free_slot].expiry_epoch = expiry_epoch;

    save_to_nvs_unlocked();
    sync_unlock(LOCK_LEVEL_USER_DB);
    return ESP_OK;
}

esp_err_t user_db_remove(const char *uid)
{
    if (uid == NULL || uid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char norm[CFG_RFID_UID_MAX_LEN];
    normalize_uid(uid, norm, sizeof(norm));

    esp_err_t err = sync_lock(LOCK_LEVEL_USER_DB, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    esp_err_t res = ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < CFG_USER_DB_MAX_USERS; i++) {
        if (s_users[i].in_use && strcmp(s_users[i].uid, norm) == 0) {
            memset(&s_users[i], 0, sizeof(user_record_t));
            save_to_nvs_unlocked();
            res = ESP_OK;
            break;
        }
    }

    sync_unlock(LOCK_LEVEL_USER_DB);
    return res;
}

esp_err_t user_db_set_active(const char *uid, bool active)
{
    if (uid == NULL || uid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    char norm[CFG_RFID_UID_MAX_LEN];
    normalize_uid(uid, norm, sizeof(norm));

    esp_err_t err = sync_lock(LOCK_LEVEL_USER_DB, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    esp_err_t res = ESP_ERR_NOT_FOUND;
    for (size_t i = 0; i < CFG_USER_DB_MAX_USERS; i++) {
        if (s_users[i].in_use && strcmp(s_users[i].uid, norm) == 0) {
            s_users[i].active = active;
            save_to_nvs_unlocked();
            res = ESP_OK;
            break;
        }
    }

    sync_unlock(LOCK_LEVEL_USER_DB);
    return res;
}

size_t user_db_list(user_record_t *out_array, size_t max_items)
{
    if (out_array == NULL || max_items == 0) {
        return 0;
    }

    if (sync_lock(LOCK_LEVEL_USER_DB, CFG_LOCK_TIMEOUT_MS) != ESP_OK) {
        return 0;
    }

    size_t count = 0;
    for (size_t i = 0; i < CFG_USER_DB_MAX_USERS && count < max_items; i++) {
        if (s_users[i].in_use) {
            out_array[count++] = s_users[i];
        }
    }

    sync_unlock(LOCK_LEVEL_USER_DB);
    return count;
}

esp_err_t user_db_reset_defaults(void)
{
    esp_err_t err = sync_lock(LOCK_LEVEL_USER_DB, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }
    seed_default_users_unlocked();
    save_to_nvs_unlocked();
    sync_unlock(LOCK_LEVEL_USER_DB);
    return ESP_OK;
}

const char *user_db_role_to_str(user_role_t role)
{
    switch (role) {
        case USER_ROLE_ADMIN:   return "ADMIN";
        case USER_ROLE_STAFF:   return "STAFF";
        case USER_ROLE_STUDENT: return "STUDENT";
        case USER_ROLE_VISITOR: return "VISITOR";
        default:                return "UNKNOWN";
    }
}

bool user_db_parse_role(const char *str, user_role_t *out_role)
{
    if (str == NULL || out_role == NULL) {
        return false;
    }
    if (strcasecmp(str, "admin") == 0) {
        *out_role = USER_ROLE_ADMIN;
        return true;
    }
    if (strcasecmp(str, "staff") == 0) {
        *out_role = USER_ROLE_STAFF;
        return true;
    }
    if (strcasecmp(str, "student") == 0) {
        *out_role = USER_ROLE_STUDENT;
        return true;
    }
    if (strcasecmp(str, "visitor") == 0) {
        *out_role = USER_ROLE_VISITOR;
        return true;
    }
    return false;
}
