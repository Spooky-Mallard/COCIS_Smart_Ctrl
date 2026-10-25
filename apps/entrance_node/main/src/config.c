#include "config.h"
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sync.h"

static const char *TAG = "CONFIG";
static const char *NVS_KEY_BLOB = "sys_cfg_v1";

static sys_config_t s_config;

static void load_compiled_defaults(sys_config_t *cfg)
{
    cfg->door_open_ms = CFG_DOOR_OPEN_DURATION_MS;
    cfg->door_alarm_ms = CFG_DOOR_HELD_ALARM_MS;
    cfg->light_grace_sec = CFG_LIGHT_GRACE_PERIOD_SEC;
    cfg->light_vacancy_sec = CFG_LIGHT_VACANCY_TIMEOUT_SEC;
    cfg->room_watts = CFG_ROOM_LIGHT_POWER_WATTS;
    cfg->auth_timeout_sec = CFG_AUTH_SESSION_TIMEOUT_SEC;
    cfg->require_admin_2fa = false;
    cfg->mqtt_enabled = true;
}

esp_err_t config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing and re-initializing NVS partition");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return err;
    }

    load_compiled_defaults(&s_config);

    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_config);
        sys_config_t stored;
        if (nvs_get_blob(h, NVS_KEY_BLOB, &stored, &sz) == ESP_OK && sz == sizeof(s_config)) {
            s_config = stored;
            ESP_LOGI(TAG, "Loaded runtime configuration from NVS");
        }
        nvs_close(h);
    } else {
        ESP_LOGI(TAG, "Using default system configuration");
    }

    return ESP_OK;
}

esp_err_t config_get(sys_config_t *out_cfg)
{
    if (out_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_CONFIG, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    *out_cfg = s_config;
    sync_unlock(LOCK_LEVEL_CONFIG);
    return ESP_OK;
}

esp_err_t config_set(const sys_config_t *new_cfg)
{
    if (new_cfg == NULL || new_cfg->door_open_ms < 500 || new_cfg->light_vacancy_sec < 2) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_CONFIG, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    s_config = *new_cfg;
    sync_unlock(LOCK_LEVEL_CONFIG);

    nvs_handle_t h;
    err = nvs_open(CFG_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY_BLOB, new_cfg, sizeof(*new_cfg));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }

    return err;
}

esp_err_t config_reset_defaults(void)
{
    sys_config_t def;
    load_compiled_defaults(&def);
    return config_set(&def);
}
