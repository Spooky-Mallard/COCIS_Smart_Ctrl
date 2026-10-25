#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define CFG_SYSTEM_NAME                 "CoCIS Smart Control System"
#define CFG_FIRMWARE_VERSION            "1.0.0-COCIS-2026"
#define CFG_TIMEZONE_POSIX              "EAT-3"
#define CFG_DEFAULT_BOOT_EPOCH          1792819800LL  /* 2026-10-24 08:30:00 EAT */

#define CFG_NVS_NAMESPACE               "cocis_cfg"
#define CFG_SD_MOUNT_POINT              "/sdcard"
#define CFG_SD_MAX_FILE_BYTES           (64 * 1024)

#define CFG_DOOR_OPEN_DURATION_MS       3000
#define CFG_DOOR_HELD_ALARM_MS          10000
#define CFG_RFID_DEBOUNCE_MS            60
#define CFG_RFID_QUEUE_DEPTH            16
#define CFG_RFID_UID_MAX_LEN            16

#define CFG_USER_DB_MAX_USERS           32
#define CFG_USER_NAME_MAX_LEN           28
#define CFG_VISITOR_DEFAULT_TTL_SEC     (24 * 3600)

#define CFG_ROOM_COUNT                  2
#define CFG_PIR_DEBOUNCE_MS             100
#define CFG_LIGHT_GRACE_PERIOD_SEC      5
#define CFG_LIGHT_VACANCY_TIMEOUT_SEC   15
#define CFG_ROOM_LIGHT_POWER_WATTS      120
#define CFG_UGX_PER_KWH                 850

#define CFG_AUTH_USER_MAX_LEN           16
#define CFG_AUTH_PASS_MAX_LEN           32
#define CFG_AUTH_SALT_LEN               16
#define CFG_AUTH_HASH_LEN               32
#define CFG_AUTH_MAX_FAILED_ATTEMPTS    3
#define CFG_AUTH_LOCKOUT_BASE_SEC       30
#define CFG_AUTH_SESSION_TIMEOUT_SEC    300
#define CFG_AUTH_DEFAULT_USER           "admin"
#define CFG_AUTH_DEFAULT_PASS           "Cocis@2026"
#define CFG_AUTH_DEFAULT_ADMIN_UID      "04A00001"

#define CFG_SUPERVISOR_OVERRIDE_TTL_SEC 300

#define CFG_WIFI_SSID                   "Wokwi-GUEST"
#define CFG_WIFI_PASSWORD               ""
#define CFG_WIFI_RECONNECT_BASE_MS      2000
#define CFG_WIFI_RECONNECT_MAX_MS       30000
#define CFG_SNTP_SERVER                 "pool.ntp.org"

#define CFG_MQTT_BROKER_HOST            "broker.hivemq.com"
#define CFG_MQTT_BROKER_PORT            1883
#define CFG_MQTT_TOPIC_PREFIX           "cocis/smart_ctrl"

#define CFG_PROTO_ACK_TIMEOUT_MS        1000
#define CFG_PROTO_MAX_RETRIES           3
#define CFG_PROTO_STORE_FWD_CAP         32
#define CFG_PROTO_HEARTBEAT_SEC         5
#define CFG_NODE_OFFLINE_THRESHOLD_SEC  15

#define CFG_LOG_QUEUE_DEPTH             32
#define CFG_LOG_RAM_RING_CAP            48

#define CFG_LOCK_TIMEOUT_MS             250

#define CFG_BTN_ENTRY_VALID_UID         "04A1B2C3"
#define CFG_BTN_EXIT_VALID_UID          "04A1B2C3"
#define CFG_BTN_INTRUDER_UID            "DEADBEEF"

typedef struct {
    uint32_t door_open_ms;
    uint32_t door_alarm_ms;
    uint32_t light_grace_sec;
    uint32_t light_vacancy_sec;
    uint32_t room_watts;
    uint32_t auth_timeout_sec;
    bool     require_admin_2fa;
    bool     mqtt_enabled;
} sys_config_t;

esp_err_t config_init(void);
esp_err_t config_get(sys_config_t *out_cfg);
esp_err_t config_set(const sys_config_t *new_cfg);
esp_err_t config_reset_defaults(void);

#endif
