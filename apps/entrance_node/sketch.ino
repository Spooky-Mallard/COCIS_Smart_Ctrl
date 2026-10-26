/**
 * ==============================================================================
 * CoCIS Smart Control System - Main Entrance Coordinator (Hub)
 * College of Computing and Information Sciences (CoCIS), Makerere University
 * Single-file standalone firmware for Wokwi Web simulation
 * Firmware: v2.0.0-HUB | Target: ESP32 | Simulator: Wokwi Web
 * ==============================================================================
 */

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psa/crypto.h"
#include "sdmmc_cmd.h"

/* ============================================================================== */
/* 2. CONSTANTS, ENUMS, TYPES & FUNCTION DECLARATIONS                            */
/* ============================================================================== */

/* --- Header: cocis_types.h --- */



typedef enum {
    NODE_ID_ENTRANCE  = 0,
    NODE_ID_ROOM_1    = 1,
    NODE_ID_ROOM_2    = 2,
    NODE_ID_COUNT     = 3,
    NODE_ID_BROADCAST = 255
} node_id_t;

typedef enum {
    PROTO_MSG_HEARTBEAT      = 0x01,
    PROTO_MSG_ROOM_EVENT     = 0x02,
    PROTO_MSG_SCHEDULE_SYNC  = 0x03,
    PROTO_MSG_EMERGENCY      = 0x04,
    PROTO_MSG_LIGHT_OVERRIDE = 0x05,
    PROTO_MSG_ACK            = 0xF0
} proto_msg_type_t;

typedef enum {
    LIGHT_STATE_OFF_VACANT   = 0,
    LIGHT_STATE_ON_ACTIVE    = 1,
    LIGHT_STATE_ON_GRACE_HOLD = 2
} light_state_t;

typedef struct {
    uint8_t  version;
    uint8_t  msg_type;
    uint8_t  src_node;
    uint8_t  dst_node;
    uint32_t seq_id;
    uint32_t ack_seq_id;
    int64_t  timestamp_epoch;
    uint8_t  room_id;
    uint8_t  occupied;
    uint8_t  light_state;
    uint8_t  emergency_active;
    uint32_t light_on_sec;
    uint32_t energy_saved_mwh;
    uint32_t uptime_sec;
    char     hmac[65];
} proto_packet_t;

typedef struct {
    node_id_t   node_id;
    const char *name;
    bool        link_simulated_online;
    bool        heartbeat_healthy;
    uint32_t    last_tx_seq;
    uint32_t    last_rx_seq;
    uint32_t    acked_count;
    uint32_t    retry_count;
    uint32_t    dup_suppressed_count;
    uint32_t    buffered_offline_count;
    uint32_t    replayed_count;
    int64_t     last_seen_uptime_sec;
    int8_t      rssi;
} node_net_status_t;



/* --- Header: cocis_config.h --- */


#define CFG_SECRET_KEY                "cocis_secure_key_2026"
#define CFG_MQTT_BROKER_HOST          "broker.hivemq.com"
#define CFG_MQTT_BROKER_PORT          1883
#define CFG_MQTT_TOPIC_PREFIX         "cocis/smart_ctrl_mak"

#define CFG_WIFI_SSID                 "Wokwi-GUEST"
#define CFG_WIFI_PASS                 ""

#define CFG_HEARTBEAT_INTERVAL_SEC    5
#define CFG_NODE_OFFLINE_TIMEOUT_SEC  15
#define CFG_MAX_STORE_FORWARD_EVENTS  32
#ifndef CFG_LOCK_TIMEOUT_MS
#define CFG_LOCK_TIMEOUT_MS           1000
#endif

#define CFG_TIMEZONE                  "EAT-3"
#define CFG_SNTP_SERVER               "pool.ntp.org"



/* --- Header: config.h --- */


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

#ifndef CFG_WIFI_SSID
#endif
#define CFG_WIFI_PASSWORD               ""
#define CFG_WIFI_RECONNECT_BASE_MS      2000
#define CFG_WIFI_RECONNECT_MAX_MS       30000
#ifndef CFG_SNTP_SERVER
#endif

#ifndef CFG_MQTT_BROKER_HOST
#endif
#ifndef CFG_MQTT_BROKER_PORT
#endif
#ifndef CFG_MQTT_TOPIC_PREFIX
#endif

#define CFG_PROTO_ACK_TIMEOUT_MS        1000
#define CFG_PROTO_MAX_RETRIES           3
#define CFG_PROTO_STORE_FWD_CAP         32
#define CFG_PROTO_HEARTBEAT_SEC         5
#define CFG_NODE_OFFLINE_THRESHOLD_SEC  15

#define CFG_LOG_QUEUE_DEPTH             32
#define CFG_LOG_RAM_RING_CAP            48

#ifndef CFG_LOCK_TIMEOUT_MS
#endif

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


/* --- Header: board_pins.h --- */



/* I2C Bus for 16x2 HD44780 LCD (PCF8574 Backpack) */
#define PIN_I2C_SDA             GPIO_NUM_21
#define PIN_I2C_SCL             GPIO_NUM_22
#define BOARD_I2C_PORT          I2C_NUM_0
#define BOARD_LCD_I2C_ADDR      0x27
#define BOARD_I2C_FREQ_HZ       100000

/* SPI2 Bus for MicroSD Card (SDSPI + FATFS) */
#define PIN_SD_CS               GPIO_NUM_5
#define PIN_SD_SCK              GPIO_NUM_18
#define PIN_SD_MOSI             GPIO_NUM_23
#define PIN_SD_MISO             GPIO_NUM_19
#define BOARD_SD_SPI_HOST       SPI2_HOST

/* Main Entrance Servo Actuators & Status LEDs */
#define PIN_SERVO_ENTRY         GPIO_NUM_13
#define PIN_SERVO_EXIT          GPIO_NUM_12
#define PIN_LED_ENTRY           GPIO_NUM_2
#define PIN_LED_EXIT            GPIO_NUM_4
#define PIN_LED_ALARM           GPIO_NUM_15
#define PIN_BUZZER              GPIO_NUM_14

/* Physical Interactive Pushbuttons */
#define PIN_BTN_ENTRY_SCAN      GPIO_NUM_32
#define PIN_BTN_EXIT_SCAN       GPIO_NUM_33
#define PIN_BTN_INTRUDER_SCAN   GPIO_NUM_27
#define PIN_BTN_EMERGENCY       GPIO_NUM_26

/* LEDC Timer & Channel Allocation */
#define BOARD_LEDC_SPEED_MODE   LEDC_LOW_SPEED_MODE
#define BOARD_LEDC_SERVO_TIMER  LEDC_TIMER_0
#define BOARD_LEDC_BUZZER_TIMER LEDC_TIMER_1

#define BOARD_LEDC_CH_ENTRY_SRV LEDC_CHANNEL_0
#define BOARD_LEDC_CH_EXIT_SRV  LEDC_CHANNEL_1
#define BOARD_LEDC_CH_BUZZER    LEDC_CHANNEL_2

esp_err_t board_pins_init(void);



/* --- Header: sync.h --- */


typedef enum {
    LOCK_LEVEL_NONE = 0,
    LOCK_LEVEL_CONFIG = 1,
    LOCK_LEVEL_USER_DB = 2,
    LOCK_LEVEL_SCHEDULE = 3,
    LOCK_LEVEL_LEDGER = 4,
    LOCK_LEVEL_ROOM = 5,
    LOCK_LEVEL_DISPLAY = 6,
    LOCK_LEVEL_SD = 7,
    LOCK_LEVEL_COUNT = 8
} sync_lock_level_t;

#define SYS_EVT_WIFI_CONNECTED   (1 << 0)
#define SYS_EVT_TIME_SYNCED      (1 << 1)
#define SYS_EVT_SD_MOUNTED       (1 << 2)
#define SYS_EVT_EMERGENCY_MODE   (1 << 3)
#define SYS_EVT_ADMIN_AUTH       (1 << 4)
#define SYS_EVT_MQTT_CONNECTED   (1 << 5)

typedef struct {
    const char *name;
    uint32_t acquire_count;
    uint32_t timeout_count;
} sync_lock_stats_t;

esp_err_t sync_init(void);
esp_err_t sync_lock(sync_lock_level_t level, uint32_t timeout_ms);
esp_err_t sync_unlock(sync_lock_level_t level);

void sync_event_set(EventBits_t bits);
void sync_event_clear(EventBits_t bits);
bool sync_event_is_set(EventBits_t bit);
EventBits_t sync_event_get_all(void);

esp_err_t sync_get_stats(sync_lock_level_t level, sync_lock_stats_t *out_stats);


/* --- Header: cocis_sync.h --- */



typedef enum {
    COCIS_LOCK_LEVEL_SYSTEM  = 0,
    COCIS_LOCK_LEVEL_NETWORK = 1,
    COCIS_LOCK_LEVEL_DATA    = 2,
    COCIS_LOCK_LEVEL_COUNT   = 3
} cocis_lock_level_t;

#define COCIS_EVT_WIFI_CONNECTED    (1 << 0)
#define COCIS_EVT_MQTT_CONNECTED    (1 << 1)
#define COCIS_EVT_TIME_SYNCED       (1 << 2)
#define COCIS_EVT_EMERGENCY_ACTIVE  (1 << 3)

esp_err_t cocis_sync_init(void);

esp_err_t cocis_sync_lock(cocis_lock_level_t level, uint32_t timeout_ms);
void cocis_sync_unlock(cocis_lock_level_t level);

void cocis_sync_event_set(uint32_t bits);
void cocis_sync_event_clear(uint32_t bits);
bool cocis_sync_event_check(uint32_t bits);
uint32_t cocis_sync_event_wait(uint32_t bits, bool clear_on_exit, bool wait_for_all, uint32_t timeout_ms);



/* --- Header: cocis_crypto.h --- */



esp_err_t cocis_crypto_init(void);

esp_err_t cocis_crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                                   const uint8_t *data, size_t data_len,
                                   uint8_t out_mac[32]);

esp_err_t cocis_crypto_hmac_hex(const char *key, const char *payload, char out_hex[65]);

bool cocis_crypto_verify_hmac(const char *key, const char *payload, const char *expected_hex);



/* --- Header: cocis_time.h --- */



esp_err_t cocis_time_init(void);

int64_t cocis_time_now_epoch(void);

void cocis_time_format_now(char *buf, size_t len);

void cocis_time_format_epoch(int64_t epoch, char *buf, size_t len);

bool cocis_time_is_synced(void);

void cocis_time_set_epoch(int64_t epoch);



/* --- Header: time_sync.h --- */


typedef struct {
    int64_t epoch;
    int     year;
    int     month;
    int     day;
    int     hour;
    int     minute;
    int     second;
    int     day_of_week; /* 0 = Sunday .. 6 = Saturday */
    bool    time_valid;
    bool    sntp_synced;
} time_snapshot_t;

esp_err_t time_sync_init(void);
esp_err_t time_sync_start_sntp(void);
esp_err_t time_sync_set_datetime(int year, int month, int day, int hour, int minute, int second);
esp_err_t time_sync_get_now(time_snapshot_t *out_now);
void      time_sync_format_iso(int64_t epoch, char *buf, size_t buf_len);
void      time_sync_format_lcd(char *buf, size_t buf_len);
bool      time_sync_is_valid(void);


/* --- Header: storage_sd.h --- */


typedef struct {
    bool     mounted;
    uint32_t access_log_bytes;
    uint32_t lighting_log_bytes;
    uint32_t audit_log_bytes;
    uint32_t write_ok_count;
    uint32_t write_err_count;
} sd_status_t;

typedef void (*sd_line_visitor_t)(const char *line, void *ctx);

esp_err_t storage_sd_init(void);
bool      storage_sd_is_mounted(void);
esp_err_t storage_sd_append_line(const char *rel_filename, const char *csv_header, const char *line);
esp_err_t storage_sd_read_lines(const char *rel_filename, sd_line_visitor_t visitor, void *ctx);
esp_err_t storage_sd_clear_logs(void);
esp_err_t storage_sd_get_status(sd_status_t *out_status);


/* --- Header: logger.h --- */


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


/* --- Header: user_db.h --- */


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


/* --- Header: schedule.h --- */


typedef struct {
    bool    enabled;
    uint8_t open_hour;
    uint8_t open_min;
    uint8_t close_hour;
    uint8_t close_min;
} day_schedule_t;

typedef struct {
    day_schedule_t days[7]; /* 0 = Sun, 1 = Mon .. 6 = Sat */
} weekly_schedule_t;

typedef enum {
    SCHED_ALLOW_NORMAL = 0,
    SCHED_ALLOW_ADMIN_BYPASS,
    SCHED_ALLOW_SUPERVISOR_OVERRIDE,
    SCHED_ALLOW_EMERGENCY_EXIT,
    SCHED_DENY_TIME_INVALID,
    SCHED_DENY_DAY_CLOSED,
    SCHED_DENY_OUTSIDE_HOURS_ENTRY,
    SCHED_DENY_OUTSIDE_HOURS_EXIT,
    SCHED_DENY_EMERGENCY_LOCKDOWN_ENTRY
} schedule_decision_t;

esp_err_t           schedule_init(void);
esp_err_t           schedule_get_weekly(weekly_schedule_t *out_sched);
esp_err_t           schedule_set_day(int day_of_week, bool enabled, uint8_t open_h, uint8_t open_m, uint8_t close_h, uint8_t close_m);
esp_err_t           schedule_reset_defaults(void);

schedule_decision_t schedule_evaluate(bool is_exit, user_role_t role, const char *uid);
bool                schedule_is_open_now(void);

esp_err_t           schedule_grant_exit_override(const char *uid);
bool                schedule_has_exit_override(const char *uid, int64_t *out_expiry_epoch);

esp_err_t           schedule_set_emergency_mode(bool active);
bool                schedule_is_emergency_mode(void);

int                 schedule_parse_day_name(const char *day_str);
const char         *schedule_day_name(int day_of_week);
const char         *schedule_decision_to_str(schedule_decision_t dec);


/* --- Header: occupancy_ledger.h --- */


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


/* --- Header: auth.h --- */


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


/* --- Header: buzzer.h --- */


typedef enum {
    BUZZER_PAT_GRANT = 0,
    BUZZER_PAT_DENY,
    BUZZER_PAT_ANTIPASSBACK,
    BUZZER_PAT_DOOR_ALARM,
    BUZZER_PAT_EMERGENCY
} buzzer_pattern_t;

esp_err_t buzzer_init(void);
esp_err_t buzzer_play(buzzer_pattern_t pattern);


/* --- Header: door.h --- */


typedef enum {
    DOOR_ID_ENTRY = 0,
    DOOR_ID_EXIT = 1,
    DOOR_ID_COUNT = 2
} door_id_t;

typedef enum {
    DOOR_STATE_LOCKED = 0,
    DOOR_STATE_UNLOCKED_TIMED,
    DOOR_STATE_EMERGENCY_OPEN,
    DOOR_STATE_FORCED_ALARM
} door_state_t;

typedef struct {
    door_state_t state;
    uint32_t     open_count;
    uint32_t     forced_alarm_count;
} door_status_t;

esp_err_t   door_init(void);
esp_err_t   door_unlock_timed(door_id_t door);
esp_err_t   door_lock_now(door_id_t door);
esp_err_t   door_set_emergency_all(bool emergency_open);
esp_err_t   door_trigger_deny_indicator(void);
esp_err_t   door_simulate_forced_hold(door_id_t door);
esp_err_t   door_get_status(door_id_t door, door_status_t *out_status);
const char *door_state_to_str(door_state_t state);


/* --- Header: rfid.h --- */


typedef enum {
    RFID_READER_ENTRY = 0,
    RFID_READER_EXIT = 1
} rfid_reader_t;

typedef struct {
    rfid_reader_t reader;
    char          uid[CFG_RFID_UID_MAX_LEN];
    bool          from_hardware_btn;
} rfid_scan_event_t;

typedef void (*rfid_scan_callback_t)(const rfid_scan_event_t *evt);
typedef void (*rfid_emergency_button_cb_t)(void);

esp_err_t rfid_init(rfid_scan_callback_t scan_cb, rfid_emergency_button_cb_t emerg_cb);
esp_err_t rfid_inject_scan(rfid_reader_t reader, const char *uid);


/* --- Header: display.h --- */


esp_err_t display_init(void);
esp_err_t display_show_alert(const char *line0, const char *line1, uint32_t duration_ms);


/* --- Header: access_control.h --- */


typedef enum {
    ACCESS_RESULT_GRANTED_NORMAL = 0,
    ACCESS_RESULT_GRANTED_ADMIN_BYPASS,
    ACCESS_RESULT_GRANTED_SUPERVISOR_EXIT,
    ACCESS_RESULT_GRANTED_EMERGENCY_EVAC,
    ACCESS_RESULT_DENIED_UNKNOWN_TAG,
    ACCESS_RESULT_DENIED_USER_INACTIVE,
    ACCESS_RESULT_DENIED_VISITOR_EXPIRED,
    ACCESS_RESULT_DENIED_OUTSIDE_SCHED_ENTRY,
    ACCESS_RESULT_DENIED_OUTSIDE_SCHED_EXIT,
    ACCESS_RESULT_DENIED_EMERGENCY_LOCKDOWN,
    ACCESS_RESULT_DENIED_ANTIPASSBACK_ENTRY,
    ACCESS_RESULT_DENIED_ANTIPASSBACK_EXIT
} access_result_t;

typedef struct {
    uint32_t granted_entry;
    uint32_t granted_exit;
    uint32_t denied_unknown;
    uint32_t denied_inactive;
    uint32_t denied_expired;
    uint32_t denied_schedule_entry;
    uint32_t denied_schedule_exit;
    uint32_t denied_antipassback;
    access_result_t last_result;
} access_stats_t;

esp_err_t       access_control_init(void);
void            access_control_handle_scan(const rfid_scan_event_t *evt);
void            access_control_handle_emergency_btn(void);
access_result_t access_control_evaluate_sync(rfid_reader_t reader, const char *uid);
esp_err_t       access_control_get_stats(access_stats_t *out_stats);
const char     *access_control_result_to_str(access_result_t res);


/* --- Header: cocis_health.h --- */



esp_err_t cocis_health_init(node_id_t my_node_id);

void cocis_health_record_rx(node_id_t src_node, uint32_t seq_id, uint32_t uptime_sec, int8_t rssi);

bool cocis_health_is_node_alive(node_id_t node_id);

const node_net_status_t *cocis_health_get_status(node_id_t node_id);

uint32_t cocis_uptime_sec(void);



/* --- Header: health.h --- */


typedef struct {
    uint32_t online_nodes;
    uint32_t free_heap_bytes;
    uint32_t uptime_sec;
} health_summary_t;

static inline esp_err_t health_init(void) {
    return cocis_health_init(NODE_ID_ENTRANCE);
}

static inline bool health_is_healthy(void) {
    return true;
}

static inline void health_report_alive(void) {
}

static inline esp_err_t health_get_summary(health_summary_t *out_sum) {
    if (out_sum) {
        uint32_t online = 1;
        if (cocis_health_is_node_alive(NODE_ID_ROOM_1)) online++;
        if (cocis_health_is_node_alive(NODE_ID_ROOM_2)) online++;
        out_sum->online_nodes = online;
        out_sum->free_heap_bytes = (uint32_t)esp_get_free_heap_size();
        out_sum->uptime_sec = cocis_uptime_sec();
    }
    return ESP_OK;
}


/* --- Header: cocis_protocol.h --- */



esp_err_t cocis_proto_init(void);

uint32_t cocis_proto_next_seq(node_id_t my_node_id);

esp_err_t cocis_proto_serialize(const proto_packet_t *pkt, const char *secret_key,
                                char *out_buf, size_t max_len);

esp_err_t cocis_proto_deserialize(const char *json_str, const char *secret_key,
                                  proto_packet_t *out_pkt);

bool cocis_proto_is_duplicate(node_id_t src_node, uint32_t seq_id);

void cocis_proto_record_seq(node_id_t src_node, uint32_t seq_id);



/* --- Header: cocis_wifi.h --- */



esp_err_t cocis_wifi_init(void);

bool cocis_wifi_is_connected(void);

int8_t cocis_wifi_get_rssi(void);

void cocis_wifi_get_ip_str(char *buf, size_t len);



/* --- Header: net_wifi.h --- */


typedef struct {
    bool   connected;
    char   ip_addr[16];
    int8_t rssi;
} wifi_status_t;

static inline esp_err_t net_wifi_init(void) {
    return cocis_wifi_init();
}

static inline bool net_wifi_is_connected(void) {
    return cocis_wifi_is_connected();
}

static inline int8_t net_wifi_get_rssi(void) {
    return cocis_wifi_get_rssi();
}

static inline void net_wifi_get_ip_str(char *buf, size_t len) {
    cocis_wifi_get_ip_str(buf, len);
}

static inline esp_err_t net_wifi_get_status(wifi_status_t *out_st) {
    if (out_st) {
        out_st->connected = cocis_wifi_is_connected();
        out_st->rssi = cocis_wifi_get_rssi();
        cocis_wifi_get_ip_str(out_st->ip_addr, sizeof(out_st->ip_addr));
    }
    return ESP_OK;
}


/* --- Header: cocis_mqtt.h --- */



typedef void (*cocis_mqtt_rx_cb_t)(const char *topic, const char *payload);

esp_err_t cocis_mqtt_init(node_id_t my_node_id);

bool cocis_mqtt_is_connected(void);

esp_err_t cocis_mqtt_publish(const char *topic, const char *payload);

esp_err_t cocis_mqtt_subscribe(const char *topic, cocis_mqtt_rx_cb_t callback);



/* --- Header: net_protocol.h --- */



typedef struct {
    node_id_t   node_id;
    const char *name;
    bool        heartbeat_healthy;
    uint32_t    last_rx_seq;
    uint32_t    acked_count;
    uint32_t    dup_suppressed_count;
    int64_t     last_seen_uptime_sec;
    int8_t      rssi;
    uint8_t     last_room_occupied;
    uint8_t     last_light_state;
} entrance_room_telemetry_t;

esp_err_t net_protocol_init(void);

esp_err_t net_protocol_send_override(uint8_t room_id, uint8_t state);

esp_err_t net_protocol_send_emergency(bool active);

esp_err_t net_protocol_send_schedule_sync(void);

esp_err_t net_protocol_get_status(node_id_t node_id, node_net_status_t *out_status);

static inline esp_err_t net_protocol_get_node_status(node_id_t node_id, node_net_status_t *out_status) {
    return net_protocol_get_status(node_id, out_status);
}

static inline bool net_protocol_is_mqtt_connected(void) {
    return cocis_mqtt_is_connected();
}

esp_err_t net_protocol_set_node_online(node_id_t node_id, bool online);

static inline esp_err_t net_protocol_broadcast_emergency(bool active) {
    return net_protocol_send_emergency(active);
}

static inline esp_err_t net_protocol_broadcast_schedule_sync(void) {
    return net_protocol_send_schedule_sync();
}

const entrance_room_telemetry_t *net_protocol_get_room_telemetry(uint8_t room_id);



/* --- Header: light.h --- */



typedef enum {
    LIGHT_FSM_OFF_VACANT = 0,
    LIGHT_FSM_ON_ACTIVE = 1,
    LIGHT_FSM_ON_GRACE_HOLD = 2
} light_fsm_state_t;

typedef struct {
    uint8_t           room_id;
    bool              pir_motion_active;
    bool              relay_on;
    light_fsm_state_t fsm_state;
    uint32_t          seconds_until_off;
    uint32_t          total_light_on_sec;
    uint32_t          total_occupied_sec;
    uint32_t          total_vacant_off_sec;
    uint32_t          energy_saved_mwh;
    uint32_t          transition_count;
} room_light_status_t;

esp_err_t   light_init(void);
esp_err_t   light_get_room_status(uint8_t room_id, room_light_status_t *out_status);
uint32_t    light_get_total_energy_saved_mwh(void);
const char *light_fsm_to_str(light_fsm_state_t state);
void        light_tick_1hz(void);
esp_err_t   light_notify_motion(uint8_t room_id, bool motion_detected);



/* --- Header: pir_sensor.h --- */



esp_err_t pir_sensor_init(void);

esp_err_t pir_sensor_simulate_motion(uint8_t room_id, bool active);



/* --- Header: cli.h --- */


esp_err_t cli_init(void);
esp_err_t cli_execute_line(char *line);


/* Global Logging Tag */
static const char *TAG = "ENTRANCE_HUB";

#ifdef __cplusplus
extern "C" {
#endif
void setup(void);
void loop(void);
#ifdef __cplusplus
}
#endif

/* ============================================================================== */
/* 3. SUBSYSTEM IMPLEMENTATIONS                                                  */
/* ============================================================================== */

/* ------------------------------------------------------------------------------ */
/* Subsystem 1: board_pins.c */
/* ------------------------------------------------------------------------------ */


esp_err_t board_pins_init(void)
{
    uint64_t out_mask = (1ULL << PIN_LED_ENTRY) |
                        (1ULL << PIN_LED_EXIT)  |
                        (1ULL << PIN_LED_ALARM);

    gpio_config_t out_cfg = {
        .pin_bit_mask = out_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&out_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure output pins: %s", esp_err_to_name(err));
        return err;
    }

    gpio_set_level(PIN_LED_ENTRY, 0);
    gpio_set_level(PIN_LED_EXIT, 0);
    gpio_set_level(PIN_LED_ALARM, 0);

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Entrance Hub GPIO outputs and ISR service initialized");
    return ESP_OK;
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 2: sync.c */
/* ------------------------------------------------------------------------------ */


static SemaphoreHandle_t s_mutexes[LOCK_LEVEL_COUNT] = { NULL };
static EventGroupHandle_t s_sys_events = NULL;
static uint32_t s_acquire_counts[LOCK_LEVEL_COUNT] = { 0 };
static uint32_t s_timeout_counts[LOCK_LEVEL_COUNT] = { 0 };

static const char *const s_lock_names[LOCK_LEVEL_COUNT] = {
    "NONE",
    "L1_CONFIG",
    "L2_USER_DB",
    "L3_SCHEDULE",
    "L4_LEDGER",
    "L5_ROOM",
    "L6_DISPLAY",
    "L7_SD"
};

esp_err_t sync_init(void)
{
    if (s_sys_events == NULL) {
        s_sys_events = xEventGroupCreate();
        if (s_sys_events == NULL) {
            ESP_LOGE(TAG, "Failed to create system event group");
            return ESP_ERR_NO_MEM;
        }
    }

    for (int i = LOCK_LEVEL_CONFIG; i < LOCK_LEVEL_COUNT; i++) {
        if (s_mutexes[i] == NULL) {
            s_mutexes[i] = xSemaphoreCreateMutex();
            if (s_mutexes[i] == NULL) {
                ESP_LOGE(TAG, "Failed to create mutex for %s", s_lock_names[i]);
                return ESP_ERR_NO_MEM;
            }
        }
    }

    ESP_LOGI(TAG, "Initialized %d hierarchical mutexes and event group", LOCK_LEVEL_COUNT - 1);
    return ESP_OK;
}

esp_err_t sync_lock(sync_lock_level_t level, uint32_t timeout_ms)
{
    if (level <= LOCK_LEVEL_NONE || level >= LOCK_LEVEL_COUNT || s_mutexes[level] == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_mutexes[level], ticks) != pdTRUE) {
        s_timeout_counts[level]++;
        ESP_LOGW(TAG, "Lock timeout on %s after %lu ms", s_lock_names[level], (unsigned long)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }

    s_acquire_counts[level]++;
    return ESP_OK;
}

esp_err_t sync_unlock(sync_lock_level_t level)
{
    if (level <= LOCK_LEVEL_NONE || level >= LOCK_LEVEL_COUNT || s_mutexes[level] == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreGive(s_mutexes[level]) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

void sync_event_set(EventBits_t bits)
{
    if (s_sys_events != NULL) {
        xEventGroupSetBits(s_sys_events, bits);
    }
}

void sync_event_clear(EventBits_t bits)
{
    if (s_sys_events != NULL) {
        xEventGroupClearBits(s_sys_events, bits);
    }
}

bool sync_event_is_set(EventBits_t bit)
{
    if (s_sys_events == NULL) {
        return false;
    }
    return (xEventGroupGetBits(s_sys_events) & bit) != 0;
}

EventBits_t sync_event_get_all(void)
{
    if (s_sys_events == NULL) {
        return 0;
    }
    return xEventGroupGetBits(s_sys_events);
}

esp_err_t sync_get_stats(sync_lock_level_t level, sync_lock_stats_t *out_stats)
{
    if (level <= LOCK_LEVEL_NONE || level >= LOCK_LEVEL_COUNT || out_stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    out_stats->name = s_lock_names[level];
    out_stats->acquire_count = s_acquire_counts[level];
    out_stats->timeout_count = s_timeout_counts[level];
    return ESP_OK;
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 3: config.c */
/* ------------------------------------------------------------------------------ */

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


/* ------------------------------------------------------------------------------ */
/* Subsystem 4: time_sync.c */
/* ------------------------------------------------------------------------------ */


static bool s_time_valid = false;
static bool s_sntp_started = false;
static bool s_sntp_synced = false;

static void sntp_sync_cb(struct timeval *tv)
{
    (void)tv;
    s_time_valid = true;
    s_sntp_synced = true;
    sync_event_set(SYS_EVT_TIME_SYNCED);
    ESP_LOGI(TAG, "Wall clock synchronized via SNTP");
}

esp_err_t time_sync_init(void)
{
    setenv("TZ", CFG_TIMEZONE_POSIX, 1);
    tzset();

    struct timeval tv = {
        .tv_sec = (time_t)CFG_DEFAULT_BOOT_EPOCH,
        .tv_usec = 0
    };
    settimeofday(&tv, NULL);
    s_time_valid = true;
    sync_event_set(SYS_EVT_TIME_SYNCED);

    char iso[32];
    time_sync_format_iso(CFG_DEFAULT_BOOT_EPOCH, iso, sizeof(iso));
    ESP_LOGI(TAG, "Initialized EAT (UTC+3) clock to %s", iso);
    return ESP_OK;
}

esp_err_t time_sync_start_sntp(void)
{
    if (s_sntp_started) {
        return ESP_OK;
    }

    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(CFG_SNTP_SERVER);
    sntp_cfg.sync_cb = sntp_sync_cb;
    esp_err_t err = esp_netif_sntp_init(&sntp_cfg);
    if (err == ESP_OK) {
        s_sntp_started = true;
        ESP_LOGI(TAG, "SNTP client started against %s", CFG_SNTP_SERVER);
    } else {
        ESP_LOGW(TAG, "SNTP init warning: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t time_sync_set_datetime(int year, int month, int day, int hour, int minute, int second)
{
    if (year < 2024 || year > 2099 || month < 1 || month > 12 ||
        day < 1 || day > 31 || hour < 0 || hour > 23 ||
        minute < 0 || minute > 59 || second < 0 || second > 59) {
        return ESP_ERR_INVALID_ARG;
    }

    struct tm t = { 0 };
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    t.tm_isdst = -1;

    time_t epoch = mktime(&t);
    if (epoch < 0) {
        return ESP_FAIL;
    }

    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    s_time_valid = true;
    sync_event_set(SYS_EVT_TIME_SYNCED);

    char buf[32];
    time_sync_format_iso((int64_t)epoch, buf, sizeof(buf));
    ESP_LOGI(TAG, "System clock manually set to %s EAT", buf);
    return ESP_OK;
}

esp_err_t time_sync_get_now(time_snapshot_t *out_now)
{
    if (out_now == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    time_t now = time(NULL);
    struct tm tm_info = { 0 };
    localtime_r(&now, &tm_info);

    out_now->epoch = (int64_t)now;
    out_now->year = tm_info.tm_year + 1900;
    out_now->month = tm_info.tm_mon + 1;
    out_now->day = tm_info.tm_mday;
    out_now->hour = tm_info.tm_hour;
    out_now->minute = tm_info.tm_min;
    out_now->second = tm_info.tm_sec;
    out_now->day_of_week = tm_info.tm_wday;
    out_now->time_valid = s_time_valid && (tm_info.tm_year + 1900 >= 2024);
    out_now->sntp_synced = s_sntp_synced;
    return ESP_OK;
}

void time_sync_format_iso(int64_t epoch, char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }

    time_t t = (time_t)epoch;
    struct tm tm_info = { 0 };
    localtime_r(&t, &tm_info);
    snprintf(buf, buf_len, "%04d-%02d-%02d %02d:%02d:%02d",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
}

void time_sync_format_lcd(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }

    static const char *const s_days[7] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
    time_snapshot_t now;
    time_sync_get_now(&now);
    const char *dname = (now.day_of_week >= 0 && now.day_of_week < 7) ? s_days[now.day_of_week] : "DAY";
    snprintf(buf, buf_len, "%s %02d:%02d:%02d EAT", dname, now.hour, now.minute, now.second);
}

bool time_sync_is_valid(void)
{
    time_snapshot_t now;
    time_sync_get_now(&now);
    return now.time_valid;
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 5: storage_sd.c */
/* ------------------------------------------------------------------------------ */


static bool s_mounted = false;
static sdmmc_card_t *s_card = NULL;
static uint32_t s_write_ok = 0;
static uint32_t s_write_err = 0;

static void build_path(const char *rel, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s/%s", CFG_SD_MOUNT_POINT, rel);
}

static uint32_t get_file_size_unlocked(const char *rel_filename)
{
    char path[64];
    build_path(rel_filename, path, sizeof(path));
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > 0) {
        return (uint32_t)st.st_size;
    }
    return 0;
}

esp_err_t storage_sd_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SD_MOSI,
        .miso_io_num = PIN_SD_MISO,
        .sclk_io_num = PIN_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t err = spi_bus_initialize(BOARD_SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SPI bus init returned %s (continuing with RAM fallback)", esp_err_to_name(err));
        return err;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SD_SPI_HOST;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = PIN_SD_CS;
    slot_cfg.host_id = BOARD_SD_SPI_HOST;

    err = esp_vfs_fat_sdspi_mount(CFG_SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        s_mounted = false;
        sync_event_clear(SYS_EVT_SD_MOUNTED);
        ESP_LOGW(TAG, "MicroSD not detected (%s) - operating in RAM ring-buffer mode", esp_err_to_name(err));
        return err;
    }

    s_mounted = true;
    sync_event_set(SYS_EVT_SD_MOUNTED);
    ESP_LOGI(TAG, "MicroSD mounted at %s (Card: %s)", CFG_SD_MOUNT_POINT, s_card ? s_card->cid.name : "SDSPI");
    return ESP_OK;
}

bool storage_sd_is_mounted(void)
{
    return s_mounted;
}

esp_err_t storage_sd_append_line(const char *rel_filename, const char *csv_header, const char *line)
{
    if (rel_filename == NULL || line == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_SD, 500);
    if (err != ESP_OK) {
        return err;
    }

    char path[64];
    build_path(rel_filename, path, sizeof(path));

    struct stat st;
    bool exists = (stat(path, &st) == 0);
    if (exists && st.st_size >= CFG_SD_MAX_FILE_BYTES) {
        char bak_path[72];
        snprintf(bak_path, sizeof(bak_path), "%s.old", path);
        unlink(bak_path);
        rename(path, bak_path);
        exists = false;
    }

    FILE *f = fopen(path, "a");
    if (f == NULL) {
        s_write_err++;
        sync_unlock(LOCK_LEVEL_SD);
        return ESP_FAIL;
    }

    if (!exists && csv_header != NULL) {
        fprintf(f, "%s\n", csv_header);
    }
    fprintf(f, "%s\n", line);
    fflush(f);
    fclose(f);

    s_write_ok++;
    sync_unlock(LOCK_LEVEL_SD);
    return ESP_OK;
}

esp_err_t storage_sd_read_lines(const char *rel_filename, sd_line_visitor_t visitor, void *ctx)
{
    if (rel_filename == NULL || visitor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_SD, 500);
    if (err != ESP_OK) {
        return err;
    }

    char path[64];
    build_path(rel_filename, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        sync_unlock(LOCK_LEVEL_SD);
        return ESP_ERR_NOT_FOUND;
    }

    char line[256];
    bool first = true;
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (first && strncmp(line, "seq,", 4) == 0) {
            first = false;
            continue;
        }
        first = false;
        if (len > 0) {
            visitor(line, ctx);
        }
    }

    fclose(f);
    sync_unlock(LOCK_LEVEL_SD);
    return ESP_OK;
}

esp_err_t storage_sd_clear_logs(void)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_SD, 500);
    if (err != ESP_OK) {
        return err;
    }

    static const char *const files[] = { "access.csv", "lighting.csv", "audit.csv" };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        char path[64];
        build_path(files[i], path, sizeof(path));
        unlink(path);
    }

    sync_unlock(LOCK_LEVEL_SD);
    return ESP_OK;
}

esp_err_t storage_sd_get_status(sd_status_t *out_status)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_SD, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    out_status->mounted = s_mounted;
    out_status->access_log_bytes = s_mounted ? get_file_size_unlocked("access.csv") : 0;
    out_status->lighting_log_bytes = s_mounted ? get_file_size_unlocked("lighting.csv") : 0;
    out_status->audit_log_bytes = s_mounted ? get_file_size_unlocked("audit.csv") : 0;
    out_status->write_ok_count = s_write_ok;
    out_status->write_err_count = s_write_err;

    sync_unlock(LOCK_LEVEL_SD);
    return ESP_OK;
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 6: logger.c */
/* ------------------------------------------------------------------------------ */


static QueueHandle_t s_log_queue = NULL;
static log_entry_t   s_ring[CFG_LOG_RAM_RING_CAP];
static size_t        s_ring_head = 0;
static size_t        s_ring_count = 0;
static uint32_t      s_next_seq = 1;
static char          s_chain_tip[9] = "00000000";

static void compute_chain_hash(const char *prev_hash, const log_entry_t *entry, char *out_hex9)
{
    char payload[192];
    snprintf(payload, sizeof(payload), "%s|%lu|%lld|%d|%s|%s|%s|%ld",
             prev_hash,
             (unsigned long)entry->seq_id,
             (long long)entry->timestamp,
             (int)entry->category,
             entry->tag_or_actor,
             entry->action,
             entry->detail,
             (long)entry->value);

    uint8_t digest[32] = { 0 };
    size_t digest_len = 0;
    psa_status_t st = psa_hash_compute(PSA_ALG_SHA_256,
                                       (const uint8_t *)payload,
                                       strlen(payload),
                                       digest,
                                       sizeof(digest),
                                       &digest_len);
    if (st == PSA_SUCCESS && digest_len >= 4) {
        snprintf(out_hex9, 9, "%02X%02X%02X%02X", digest[0], digest[1], digest[2], digest[3]);
    } else {
        snprintf(out_hex9, 9, "ERRHASH0");
    }
}

static void process_log_entry(log_entry_t *item)
{
    item->seq_id = s_next_seq++;
    compute_chain_hash(s_chain_tip, item, item->chain_hash);
    strncpy(s_chain_tip, item->chain_hash, sizeof(s_chain_tip) - 1);
    s_chain_tip[sizeof(s_chain_tip) - 1] = '\0';

    s_ring[s_ring_head] = *item;
    s_ring_head = (s_ring_head + 1) % CFG_LOG_RAM_RING_CAP;
    if (s_ring_count < CFG_LOG_RAM_RING_CAP) {
        s_ring_count++;
    }

    char iso_time[32];
    time_sync_format_iso(item->timestamp, iso_time, sizeof(iso_time));

    char csv_line[220];
    snprintf(csv_line, sizeof(csv_line), "%lu,%s,%lld,%s,%s,%s,%ld,%s",
             (unsigned long)item->seq_id,
             iso_time,
             (long long)item->timestamp,
             item->tag_or_actor,
             item->action,
             item->detail,
             (long)item->value,
             item->chain_hash);

    const char *fname = "access.csv";
    const char *hdr = "seq,datetime_eat,epoch,uid_or_actor,action,detail,value,sha256_chain";
    if (item->category == LOG_CAT_LIGHTING) {
        fname = "lighting.csv";
    } else if (item->category == LOG_CAT_AUDIT) {
        fname = "audit.csv";
    }

    if (storage_sd_is_mounted()) {
        storage_sd_append_line(fname, hdr, csv_line);
    }
}

static void logger_task(void *arg)
{
    (void)arg;
    log_entry_t item;
    while (1) {
        if (xQueueReceive(s_log_queue, &item, pdMS_TO_TICKS(500)) == pdTRUE) {
            process_log_entry(&item);
        }
    }
}

static esp_err_t enqueue_record(log_category_t cat, const char *actor, const char *action, const char *detail, int32_t val)
{
    if (s_log_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    log_entry_t item = { 0 };
    time_snapshot_t now;
    time_sync_get_now(&now);

    item.timestamp = now.epoch;
    item.category = cat;
    strncpy(item.tag_or_actor, actor ? actor : "-", sizeof(item.tag_or_actor) - 1);
    strncpy(item.action, action ? action : "-", sizeof(item.action) - 1);
    strncpy(item.detail, detail ? detail : "-", sizeof(item.detail) - 1);
    item.value = val;

    if (xQueueSend(s_log_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Log queue full, processing synchronously");
        process_log_entry(&item);
    }
    return ESP_OK;
}

esp_err_t logger_init(void)
{
    psa_crypto_init();

    if (s_log_queue == NULL) {
        s_log_queue = xQueueCreate(CFG_LOG_QUEUE_DEPTH, sizeof(log_entry_t));
        if (s_log_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    BaseType_t ok = xTaskCreate(logger_task, "logger_task", 6144, NULL, 3, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Async hash-chained CSV logger initialized (Genesis=%s)", s_chain_tip);
    return ESP_OK;
}

esp_err_t logger_log_access(const char *uid, const char *direction, const char *result, int32_t inside_count)
{
    return enqueue_record(LOG_CAT_ACCESS, uid, direction, result, inside_count);
}

esp_err_t logger_log_lighting(uint8_t room_id, const char *state_str, int32_t energy_saved_mwh)
{
    char actor[16];
    snprintf(actor, sizeof(actor), "ROOM_%u", (unsigned)room_id);
    return enqueue_record(LOG_CAT_LIGHTING, actor, "LIGHT_FSM", state_str, energy_saved_mwh);
}

esp_err_t logger_log_audit(const char *actor, const char *action, const char *detail)
{
    return enqueue_record(LOG_CAT_AUDIT, actor, action, detail, 0);
}

size_t logger_get_recent(log_entry_t *out_entries, size_t max_entries, const char *filter_uid)
{
    if (out_entries == NULL || max_entries == 0 || s_ring_count == 0) {
        return 0;
    }

    size_t matched = 0;
    size_t start = (s_ring_head + CFG_LOG_RAM_RING_CAP - s_ring_count) % CFG_LOG_RAM_RING_CAP;

    for (size_t i = 0; i < s_ring_count; i++) {
        size_t idx = (start + s_ring_count - 1 - i) % CFG_LOG_RAM_RING_CAP;
        const log_entry_t *e = &s_ring[idx];
        if (filter_uid != NULL && filter_uid[0] != '\0') {
            if (strcasecmp(e->tag_or_actor, filter_uid) != 0) {
                continue;
            }
        }
        out_entries[matched++] = *e;
        if (matched >= max_entries) {
            break;
        }
    }
    return matched;
}

void logger_get_chain_tip(char *out_hash, size_t buf_len)
{
    if (out_hash != NULL && buf_len > 0) {
        snprintf(out_hash, buf_len, "%s", s_chain_tip);
    }
}

esp_err_t logger_verify_integrity(uint32_t *out_checked, bool *out_valid)
{
    logger_flush_sync(200);

    size_t start = (s_ring_head + CFG_LOG_RAM_RING_CAP - s_ring_count) % CFG_LOG_RAM_RING_CAP;
    char running_hash[9] = "00000000";
    bool valid = true;
    uint32_t count = 0;

    if (s_ring_count < s_next_seq - 1 && s_ring_count > 0) {
        /* Ring wrapped; verify contiguous tail inside ring */
        const log_entry_t *first = &s_ring[start];
        strncpy(running_hash, first->chain_hash, sizeof(running_hash) - 1);
        running_hash[8] = '\0';
        count = 1;
        for (size_t i = 1; i < s_ring_count; i++) {
            size_t idx = (start + i) % CFG_LOG_RAM_RING_CAP;
            char expected[9];
            compute_chain_hash(running_hash, &s_ring[idx], expected);
            if (strcmp(expected, s_ring[idx].chain_hash) != 0) {
                valid = false;
                break;
            }
            strncpy(running_hash, expected, sizeof(running_hash));
            count++;
        }
    } else {
        for (size_t i = 0; i < s_ring_count; i++) {
            size_t idx = (start + i) % CFG_LOG_RAM_RING_CAP;
            char expected[9];
            compute_chain_hash(running_hash, &s_ring[idx], expected);
            if (strcmp(expected, s_ring[idx].chain_hash) != 0) {
                valid = false;
                break;
            }
            strncpy(running_hash, expected, sizeof(running_hash));
            count++;
        }
    }

    if (out_checked != NULL) {
        *out_checked = count;
    }
    if (out_valid != NULL) {
        *out_valid = valid;
    }
    return ESP_OK;
}

esp_err_t logger_reset_chain(void)
{
    logger_flush_sync(200);
    s_ring_head = 0;
    s_ring_count = 0;
    s_next_seq = 1;
    snprintf(s_chain_tip, sizeof(s_chain_tip), "00000000");
    if (storage_sd_is_mounted()) {
        storage_sd_clear_logs();
    }
    return ESP_OK;
}

void logger_flush_sync(uint32_t wait_ms)
{
    if (s_log_queue == NULL) {
        return;
    }
    uint32_t elapsed = 0;
    while (uxQueueMessagesWaiting(s_log_queue) > 0 && elapsed < wait_ms) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed += 10;
    }
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 7: user_db.c */
/* ------------------------------------------------------------------------------ */

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


/* ------------------------------------------------------------------------------ */
/* Subsystem 8: schedule.c */
/* ------------------------------------------------------------------------------ */

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


/* ------------------------------------------------------------------------------ */
/* Subsystem 9: occupancy_ledger.c */
/* ------------------------------------------------------------------------------ */


static occupant_record_t s_records[CFG_USER_DB_MAX_USERS];
static occupancy_stats_t s_ledger_stats = { 0 };

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
        s_ledger_stats.current_inside++;
        if (s_ledger_stats.current_inside > s_ledger_stats.peak_inside) {
            s_ledger_stats.peak_inside = s_ledger_stats.current_inside;
        }
    } else if (strcmp(tok_act, "EXIT") == 0 && slot->is_inside) {
        slot->is_inside = false;
        slot->last_exit_epoch = ep;
        if (s_ledger_stats.current_inside > 0) {
            s_ledger_stats.current_inside--;
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
    memset(&s_ledger_stats, 0, sizeof(s_ledger_stats));
    sync_unlock(LOCK_LEVEL_LEDGER);

    if (storage_sd_is_mounted()) {
        if (storage_sd_read_lines("access.csv", boot_recovery_visitor, NULL) == ESP_OK) {
            s_ledger_stats.recovered_on_boot = s_ledger_stats.current_inside;
            if (s_ledger_stats.recovered_on_boot > 0) {
                ESP_LOGI(TAG, "Recovered %lu occupant(s) inside CoCIS from SD access.csv",
                         (unsigned long)s_ledger_stats.recovered_on_boot);
            }
        }
    }

    ESP_LOGI(TAG, "Occupancy ledger initialized (currently inside: %lu)", (unsigned long)s_ledger_stats.current_inside);
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
        s_ledger_stats.antipassback_blocks++;
        sync_unlock(LOCK_LEVEL_LEDGER);
        return ESP_ERR_INVALID_STATE;
    }

    strncpy(slot->uid, user->uid, sizeof(slot->uid) - 1);
    strncpy(slot->name, user->name, sizeof(slot->name) - 1);
    slot->role = user->role;
    slot->is_inside = true;
    slot->entry_epoch = now_epoch;
    slot->visit_count++;

    s_ledger_stats.current_inside++;
    s_ledger_stats.total_entries++;
    if (s_ledger_stats.current_inside > s_ledger_stats.peak_inside) {
        s_ledger_stats.peak_inside = s_ledger_stats.current_inside;
    }

    if (out_new_count != NULL) {
        *out_new_count = s_ledger_stats.current_inside;
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
        s_ledger_stats.antipassback_blocks++;
        sync_unlock(LOCK_LEVEL_LEDGER);
        return ESP_ERR_INVALID_STATE;
    }

    slot->is_inside = false;
    slot->last_exit_epoch = now_epoch;
    if (s_ledger_stats.current_inside > 0) {
        s_ledger_stats.current_inside--;
    }
    s_ledger_stats.total_exits++;

    if (out_new_count != NULL) {
        *out_new_count = s_ledger_stats.current_inside;
    }

    sync_unlock(LOCK_LEVEL_LEDGER);
    return ESP_OK;
}

void occupancy_ledger_record_apb_violation(void)
{
    if (sync_lock(LOCK_LEVEL_LEDGER, CFG_LOCK_TIMEOUT_MS) == ESP_OK) {
        s_ledger_stats.antipassback_blocks++;
        sync_unlock(LOCK_LEVEL_LEDGER);
    }
}

uint32_t occupancy_ledger_get_count(void)
{
    uint32_t cnt = 0;
    if (sync_lock(LOCK_LEVEL_LEDGER, CFG_LOCK_TIMEOUT_MS) == ESP_OK) {
        cnt = s_ledger_stats.current_inside;
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

    *out_stats = s_ledger_stats;
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
    memset(&s_ledger_stats, 0, sizeof(s_ledger_stats));
    sync_unlock(LOCK_LEVEL_LEDGER);
    return ESP_OK;
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 10: auth.c */
/* ------------------------------------------------------------------------------ */

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


/* ------------------------------------------------------------------------------ */
/* Subsystem 11: buzzer.c */
/* ------------------------------------------------------------------------------ */


static QueueHandle_t s_buzzer_queue = NULL;

static void tone_burst(uint32_t freq_hz, uint32_t duration_ms)
{
    if (freq_hz > 0) {
        ledc_set_freq(BOARD_LEDC_SPEED_MODE, BOARD_LEDC_BUZZER_TIMER, freq_hz);
        ledc_set_duty(BOARD_LEDC_SPEED_MODE, BOARD_LEDC_CH_BUZZER, 512);
        ledc_update_duty(BOARD_LEDC_SPEED_MODE, BOARD_LEDC_CH_BUZZER);
    }
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    ledc_set_duty(BOARD_LEDC_SPEED_MODE, BOARD_LEDC_CH_BUZZER, 0);
    ledc_update_duty(BOARD_LEDC_SPEED_MODE, BOARD_LEDC_CH_BUZZER);
}

static void buzzer_task(void *arg)
{
    (void)arg;
    buzzer_pattern_t pat;
    while (1) {
        if (xQueueReceive(s_buzzer_queue, &pat, portMAX_DELAY) == pdTRUE) {
            switch (pat) {
                case BUZZER_PAT_GRANT:
                    tone_burst(2200, 90);
                    break;
                case BUZZER_PAT_DENY:
                    tone_burst(450, 120);
                    vTaskDelay(pdMS_TO_TICKS(60));
                    tone_burst(380, 160);
                    break;
                case BUZZER_PAT_ANTIPASSBACK:
                    for (int i = 0; i < 3; i++) {
                        tone_burst(900, 70);
                        vTaskDelay(pdMS_TO_TICKS(40));
                    }
                    break;
                case BUZZER_PAT_DOOR_ALARM:
                    for (int i = 0; i < 4; i++) {
                        tone_burst(1800, 100);
                        tone_burst(800, 100);
                    }
                    break;
                case BUZZER_PAT_EMERGENCY:
                    for (int i = 0; i < 3; i++) {
                        tone_burst(2600, 140);
                        tone_burst(1200, 140);
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

esp_err_t buzzer_init(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode = BOARD_LEDC_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = BOARD_LEDC_BUZZER_TIMER,
        .freq_hz = 2000,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        return err;
    }

    ledc_channel_config_t ccfg = {
        .gpio_num = PIN_BUZZER,
        .speed_mode = BOARD_LEDC_SPEED_MODE,
        .channel = BOARD_LEDC_CH_BUZZER,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BOARD_LEDC_BUZZER_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    err = ledc_channel_config(&ccfg);
    if (err != ESP_OK) {
        return err;
    }

    if (s_buzzer_queue == NULL) {
        s_buzzer_queue = xQueueCreate(8, sizeof(buzzer_pattern_t));
        if (s_buzzer_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (xTaskCreate(buzzer_task, "buzzer_task", 3072, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "LEDC piezo buzzer pattern engine initialized on GPIO %d", (int)PIN_BUZZER);
    return ESP_OK;
}

esp_err_t buzzer_play(buzzer_pattern_t pattern)
{
    if (s_buzzer_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xQueueSend(s_buzzer_queue, &pattern, 0);
    return ESP_OK;
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 12: door.c */
/* ------------------------------------------------------------------------------ */


/* 50 Hz servo (20 ms period) with 13-bit duty (8192 steps):
 * 0 deg (~1.0 ms pulse) = 8192 * (1.0 / 20.0) = 410
 * 90 deg (~1.5 ms pulse) = 8192 * (1.5 / 20.0) = 614
 */
#define SERVO_DUTY_CLOSED_0DEG   410
#define SERVO_DUTY_OPEN_90DEG    614

static door_status_t s_doors[DOOR_ID_COUNT];
static TimerHandle_t s_relock_timers[DOOR_ID_COUNT] = { NULL, NULL };
static TimerHandle_t s_alarm_led_timer = NULL;

static void apply_servo_and_led(door_id_t door, bool open)
{
    ledc_channel_t ch = (door == DOOR_ID_ENTRY) ? BOARD_LEDC_CH_ENTRY_SRV : BOARD_LEDC_CH_EXIT_SRV;
    gpio_num_t led_pin = (door == DOOR_ID_ENTRY) ? PIN_LED_ENTRY : PIN_LED_EXIT;
    uint32_t duty = open ? SERVO_DUTY_OPEN_90DEG : SERVO_DUTY_CLOSED_0DEG;

    ledc_set_duty(BOARD_LEDC_SPEED_MODE, ch, duty);
    ledc_update_duty(BOARD_LEDC_SPEED_MODE, ch);
    gpio_set_level(led_pin, open ? 1 : 0);
}

static void relock_timer_cb(TimerHandle_t xTimer)
{
    intptr_t idx = (intptr_t)pvTimerGetTimerID(xTimer);
    if (idx >= 0 && idx < DOOR_ID_COUNT) {
        if (s_doors[idx].state != DOOR_STATE_EMERGENCY_OPEN) {
            s_doors[idx].state = DOOR_STATE_LOCKED;
            apply_servo_and_led((door_id_t)idx, false);
            ESP_LOGI(TAG, "%s door auto-locked", (idx == DOOR_ID_ENTRY) ? "ENTRY" : "EXIT");
        }
    }
}

static void alarm_led_timer_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (s_doors[DOOR_ID_ENTRY].state != DOOR_STATE_EMERGENCY_OPEN &&
        s_doors[DOOR_ID_EXIT].state != DOOR_STATE_EMERGENCY_OPEN) {
        gpio_set_level(PIN_LED_ALARM, 0);
    }
}

esp_err_t door_init(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode = BOARD_LEDC_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_13_BIT,
        .timer_num = BOARD_LEDC_SERVO_TIMER,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        return err;
    }

    const gpio_num_t pins[DOOR_ID_COUNT] = { PIN_SERVO_ENTRY, PIN_SERVO_EXIT };
    const ledc_channel_t chs[DOOR_ID_COUNT] = { BOARD_LEDC_CH_ENTRY_SRV, BOARD_LEDC_CH_EXIT_SRV };

    for (int i = 0; i < DOOR_ID_COUNT; i++) {
        ledc_channel_config_t ccfg = {
            .gpio_num = pins[i],
            .speed_mode = BOARD_LEDC_SPEED_MODE,
            .channel = chs[i],
            .intr_type = LEDC_INTR_DISABLE,
            .timer_sel = BOARD_LEDC_SERVO_TIMER,
            .duty = SERVO_DUTY_CLOSED_0DEG,
            .hpoint = 0
        };
        err = ledc_channel_config(&ccfg);
        if (err != ESP_OK) {
            return err;
        }
        s_doors[i].state = DOOR_STATE_LOCKED;
        s_doors[i].open_count = 0;
        s_doors[i].forced_alarm_count = 0;
        apply_servo_and_led((door_id_t)i, false);

        s_relock_timers[i] = xTimerCreate("door_tmr",
                                          pdMS_TO_TICKS(CFG_DOOR_OPEN_DURATION_MS),
                                          pdFALSE,
                                          (void *)(intptr_t)i,
                                          relock_timer_cb);
    }

    s_alarm_led_timer = xTimerCreate("alm_led", pdMS_TO_TICKS(1200), pdFALSE, NULL, alarm_led_timer_cb);
    ESP_LOGI(TAG, "Entry (GPIO %d) and Exit (GPIO %d) servo door actuators initialized",
             (int)PIN_SERVO_ENTRY, (int)PIN_SERVO_EXIT);
    return ESP_OK;
}

esp_err_t door_unlock_timed(door_id_t door)
{
    if (door < 0 || door >= DOOR_ID_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    sys_config_t cfg;
    config_get(&cfg);

    s_doors[door].open_count++;
    if (s_doors[door].state != DOOR_STATE_EMERGENCY_OPEN) {
        s_doors[door].state = DOOR_STATE_UNLOCKED_TIMED;
    }
    apply_servo_and_led(door, true);

    if (s_relock_timers[door] != NULL) {
        xTimerChangePeriod(s_relock_timers[door], pdMS_TO_TICKS(cfg.door_open_ms), 0);
        xTimerStart(s_relock_timers[door], 0);
    }
    return ESP_OK;
}

esp_err_t door_lock_now(door_id_t door)
{
    if (door < 0 || door >= DOOR_ID_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_relock_timers[door] != NULL) {
        xTimerStop(s_relock_timers[door], 0);
    }
    s_doors[door].state = DOOR_STATE_LOCKED;
    apply_servo_and_led(door, false);
    return ESP_OK;
}

esp_err_t door_set_emergency_all(bool emergency_open)
{
    for (int i = 0; i < DOOR_ID_COUNT; i++) {
        if (s_relock_timers[i] != NULL) {
            xTimerStop(s_relock_timers[i], 0);
        }
        s_doors[i].state = emergency_open ? DOOR_STATE_EMERGENCY_OPEN : DOOR_STATE_LOCKED;
        apply_servo_and_led((door_id_t)i, emergency_open);
    }
    gpio_set_level(PIN_LED_ALARM, emergency_open ? 1 : 0);
    if (emergency_open) {
        buzzer_play(BUZZER_PAT_EMERGENCY);
    }
    return ESP_OK;
}

esp_err_t door_trigger_deny_indicator(void)
{
    gpio_set_level(PIN_LED_ALARM, 1);
    if (s_alarm_led_timer != NULL) {
        xTimerStart(s_alarm_led_timer, 0);
    }
    return ESP_OK;
}

esp_err_t door_simulate_forced_hold(door_id_t door)
{
    if (door < 0 || door >= DOOR_ID_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    s_doors[door].state = DOOR_STATE_FORCED_ALARM;
    s_doors[door].forced_alarm_count++;
    gpio_set_level(PIN_LED_ALARM, 1);
    buzzer_play(BUZZER_PAT_DOOR_ALARM);

    const char *dname = (door == DOOR_ID_ENTRY) ? "ENTRY_DOOR" : "EXIT_DOOR";
    logger_log_audit(dname, "TAILGATE_FORCED_ALARM", "Door held open / forced without valid scan");

    if (s_relock_timers[door] != NULL) {
        xTimerChangePeriod(s_relock_timers[door], pdMS_TO_TICKS(2500), 0);
        xTimerStart(s_relock_timers[door], 0);
    }
    if (s_alarm_led_timer != NULL) {
        xTimerStart(s_alarm_led_timer, 0);
    }
    return ESP_OK;
}

esp_err_t door_get_status(door_id_t door, door_status_t *out_status)
{
    if (door < 0 || door >= DOOR_ID_COUNT || out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_status = s_doors[door];
    return ESP_OK;
}

const char *door_state_to_str(door_state_t state)
{
    switch (state) {
        case DOOR_STATE_LOCKED:         return "LOCKED (Closed)";
        case DOOR_STATE_UNLOCKED_TIMED: return "OPEN (Granted)";
        case DOOR_STATE_EMERGENCY_OPEN: return "EMERGENCY UNLOCKED";
        case DOOR_STATE_FORCED_ALARM:   return "ALARM (Forced/Held)";
        default:                        return "UNKNOWN";
    }
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 13: rfid.c */
/* ------------------------------------------------------------------------------ */


static QueueHandle_t              s_btn_isr_queue = NULL;
static rfid_scan_callback_t       s_scan_cb = NULL;
static rfid_emergency_button_cb_t s_emerg_cb = NULL;

static void IRAM_ATTR button_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)(uintptr_t)arg;
    BaseType_t high_task_woken = pdFALSE;
    if (s_btn_isr_queue != NULL) {
        xQueueSendFromISR(s_btn_isr_queue, &gpio_num, &high_task_woken);
    }
    portYIELD_FROM_ISR(high_task_woken);
}

static void rfid_task(void *arg)
{
    (void)arg;
    int64_t last_press_ms[40] = { 0 };
    uint32_t gpio_num = 0;

    while (1) {
        if (xQueueReceive(s_btn_isr_queue, &gpio_num, portMAX_DELAY) == pdTRUE) {
            if (gpio_num >= 40) {
                continue;
            }
            int64_t now_ms = esp_timer_get_time() / 1000LL;
            if ((now_ms - last_press_ms[gpio_num]) < CFG_RFID_DEBOUNCE_MS) {
                continue;
            }
            last_press_ms[gpio_num] = now_ms;

            if (gpio_num == PIN_BTN_EMERGENCY) {
                if (s_emerg_cb != NULL) {
                    s_emerg_cb();
                }
                continue;
            }

            rfid_scan_event_t evt = { 0 };
            evt.from_hardware_btn = true;
            if (gpio_num == PIN_BTN_ENTRY_SCAN) {
                evt.reader = RFID_READER_ENTRY;
                strncpy(evt.uid, CFG_BTN_ENTRY_VALID_UID, sizeof(evt.uid) - 1);
            } else if (gpio_num == PIN_BTN_EXIT_SCAN) {
                evt.reader = RFID_READER_EXIT;
                strncpy(evt.uid, CFG_BTN_EXIT_VALID_UID, sizeof(evt.uid) - 1);
            } else if (gpio_num == PIN_BTN_INTRUDER_SCAN) {
                evt.reader = RFID_READER_ENTRY;
                strncpy(evt.uid, CFG_BTN_INTRUDER_UID, sizeof(evt.uid) - 1);
            } else {
                continue;
            }

            if (s_scan_cb != NULL) {
                s_scan_cb(&evt);
            }
        }
    }
}

esp_err_t rfid_init(rfid_scan_callback_t scan_cb, rfid_emergency_button_cb_t emerg_cb)
{
    s_scan_cb = scan_cb;
    s_emerg_cb = emerg_cb;

    if (s_btn_isr_queue == NULL) {
        s_btn_isr_queue = xQueueCreate(CFG_RFID_QUEUE_DEPTH, sizeof(uint32_t));
        if (s_btn_isr_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    uint64_t btn_mask = (1ULL << PIN_BTN_ENTRY_SCAN)    |
                        (1ULL << PIN_BTN_EXIT_SCAN)     |
                        (1ULL << PIN_BTN_INTRUDER_SCAN) |
                        (1ULL << PIN_BTN_EMERGENCY);

    gpio_config_t bcfg = {
        .pin_bit_mask = btn_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };
    esp_err_t err = gpio_config(&bcfg);
    if (err != ESP_OK) {
        return err;
    }

    const gpio_num_t btns[4] = {
        PIN_BTN_ENTRY_SCAN,
        PIN_BTN_EXIT_SCAN,
        PIN_BTN_INTRUDER_SCAN,
        PIN_BTN_EMERGENCY
    };
    for (int i = 0; i < 4; i++) {
        gpio_isr_handler_add(btns[i], button_isr_handler, (void *)(uintptr_t)btns[i]);
    }

    if (xTaskCreate(rfid_task, "rfid_task", 4096, NULL, 7, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Entry/Exit RFID reader abstraction and button ISRs initialized");
    return ESP_OK;
}

esp_err_t rfid_inject_scan(rfid_reader_t reader, const char *uid)
{
    if (uid == NULL || uid[0] == '\0' || (reader != RFID_READER_ENTRY && reader != RFID_READER_EXIT)) {
        return ESP_ERR_INVALID_ARG;
    }

    rfid_scan_event_t evt = { 0 };
    evt.reader = reader;
    evt.from_hardware_btn = false;

    size_t i = 0;
    while (uid[i] != '\0' && i + 1 < sizeof(evt.uid)) {
        evt.uid[i] = (char)toupper((unsigned char)uid[i]);
        i++;
    }
    evt.uid[i] = '\0';

    if (s_scan_cb != NULL) {
        s_scan_cb(&evt);
    }
    return ESP_OK;
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 14: display.c */
/* ------------------------------------------------------------------------------ */


#define LCD_BACKLIGHT 0x08
#define LCD_ENABLE    0x04
#define LCD_RS_DATA   0x01

static i2c_master_dev_handle_t s_lcd_dev = NULL;
static bool                    s_lcd_ready = false;
static char                    s_alert_line0[17] = { 0 };
static char                    s_alert_line1[17] = { 0 };
static int64_t                 s_alert_until_ms = 0;

static void lcd_write_nibble(uint8_t nibble, uint8_t rs_flag)
{
    if (!s_lcd_ready || s_lcd_dev == NULL) {
        return;
    }
    uint8_t base = (uint8_t)((nibble << 4) | (rs_flag ? LCD_RS_DATA : 0x00) | LCD_BACKLIGHT);
    uint8_t pulse[2] = { (uint8_t)(base | LCD_ENABLE), base };
    i2c_master_transmit(s_lcd_dev, pulse, sizeof(pulse), 50);
    esp_rom_delay_us(50);
}

static void lcd_write_byte(uint8_t val, uint8_t rs_flag)
{
    lcd_write_nibble((uint8_t)(val >> 4), rs_flag);
    lcd_write_nibble((uint8_t)(val & 0x0F), rs_flag);
}

static void lcd_cmd(uint8_t cmd)
{
    lcd_write_byte(cmd, 0);
    if (cmd == 0x01 || cmd == 0x02) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

static void lcd_draw_row(uint8_t row, const char *text)
{
    uint8_t addr = (row == 0) ? 0x80 : 0xC0;
    lcd_cmd(addr);

    char padded[17];
    snprintf(padded, sizeof(padded), "%-16.16s", text ? text : "");
    for (int i = 0; i < 16; i++) {
        lcd_write_byte((uint8_t)padded[i], 1);
    }
}

static void display_task(void *arg)
{
    (void)arg;
    uint8_t page = 0;
    int64_t last_page_switch_ms = 0;
    char prev0[17] = { 0 };
    char prev1[17] = { 0 };

    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000LL;
        char line0[17] = { 0 };
        char line1[17] = { 0 };

        if (sync_lock(LOCK_LEVEL_DISPLAY, 100) == ESP_OK) {
            if (schedule_is_emergency_mode()) {
                uint32_t inside = occupancy_ledger_get_count();
                snprintf(line0, sizeof(line0), "!! EMERGENCY !! ");
                snprintf(line1, sizeof(line1), "EVAC! IN:%lu      ", (unsigned long)inside);
            } else if (now_ms < s_alert_until_ms) {
                snprintf(line0, sizeof(line0), "%-16.16s", s_alert_line0);
                snprintf(line1, sizeof(line1), "%-16.16s", s_alert_line1);
            } else {
                if ((now_ms - last_page_switch_ms) >= 3000) {
                    page = (uint8_t)((page + 1) % 3);
                    last_page_switch_ms = now_ms;
                }

                if (page == 0) {
                    uint32_t inside = occupancy_ledger_get_count();
                    bool open = schedule_is_open_now();
                    snprintf(line0, sizeof(line0), "CoCIS Smart Ctrl");
                    snprintf(line1, sizeof(line1), "IN:%-2lu  SCHED:%s",
                             (unsigned long)inside, open ? "OPN" : "CLS");
                } else if (page == 1) {
                    room_light_status_t r1 = { 0 }, r2 = { 0 };
                    light_get_room_status(1, &r1);
                    light_get_room_status(2, &r2);
                    snprintf(line0, sizeof(line0), "R1:%s L1:%s   ",
                             r1.pir_motion_active ? "OCC" : "VAC",
                             r1.relay_on ? "ON " : "OFF");
                    snprintf(line1, sizeof(line1), "R2:%s L2:%s   ",
                             r2.pir_motion_active ? "OCC" : "VAC",
                             r2.relay_on ? "ON " : "OFF");
                } else {
                    time_sync_format_lcd(line0, sizeof(line0));
                    uint32_t saved = light_get_total_energy_saved_mwh();
                    snprintf(line1, sizeof(line1), "Saved:%5lumWh  ", (unsigned long)saved);
                }
            }

            if (strcmp(line0, prev0) != 0 || strcmp(line1, prev1) != 0) {
                lcd_draw_row(0, line0);
                lcd_draw_row(1, line1);
                strncpy(prev0, line0, sizeof(prev0) - 1);
                strncpy(prev1, line1, sizeof(prev1) - 1);
            }

            sync_unlock(LOCK_LEVEL_DISPLAY);
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

esp_err_t display_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = BOARD_I2C_PORT,
        .scl_io_num = PIN_I2C_SCL,
        .sda_io_num = PIN_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };
    i2c_master_bus_handle_t bus_handle = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "I2C bus init failed (%s) - continuing without LCD", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_LCD_I2C_ADDR,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ
    };
    err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_lcd_dev);
    if (err != ESP_OK) {
        return err;
    }

    s_lcd_ready = true;
    vTaskDelay(pdMS_TO_TICKS(50));
    lcd_write_nibble(0x03, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(0x03, 0);
    esp_rom_delay_us(150);
    lcd_write_nibble(0x03, 0);
    esp_rom_delay_us(150);
    lcd_write_nibble(0x02, 0);
    esp_rom_delay_us(150);

    lcd_cmd(0x28); /* 4-bit, 2 lines, 5x8 font */
    lcd_cmd(0x08); /* Display off */
    lcd_cmd(0x01); /* Clear display */
    vTaskDelay(pdMS_TO_TICKS(2));
    lcd_cmd(0x06); /* Entry mode: increment */
    lcd_cmd(0x0C); /* Display on, cursor off */

    lcd_draw_row(0, "CoCIS Smart Ctrl");
    lcd_draw_row(1, "Booting System..");

    if (xTaskCreate(display_task, "display_task", 4096, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "16x2 I2C LCD initialized at 0x%02X (SDA=%d, SCL=%d)",
             BOARD_LCD_I2C_ADDR, (int)PIN_I2C_SDA, (int)PIN_I2C_SCL);
    return ESP_OK;
}

esp_err_t display_show_alert(const char *line0, const char *line1, uint32_t duration_ms)
{
    if (sync_lock(LOCK_LEVEL_DISPLAY, CFG_LOCK_TIMEOUT_MS) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    snprintf(s_alert_line0, sizeof(s_alert_line0), "%-16.16s", line0 ? line0 : "");
    snprintf(s_alert_line1, sizeof(s_alert_line1), "%-16.16s", line1 ? line1 : "");
    s_alert_until_ms = (esp_timer_get_time() / 1000LL) + duration_ms;

    sync_unlock(LOCK_LEVEL_DISPLAY);
    return ESP_OK;
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 15: access_control.c */
/* ------------------------------------------------------------------------------ */


static QueueHandle_t  s_scan_queue = NULL;
static access_access_stats_t s_access_stats = { 0 };

static access_result_t process_scan_event(const rfid_scan_event_t *evt)
{
    bool is_exit = (evt->reader == RFID_READER_EXIT);
    const char *dir_str = is_exit ? "EXIT" : "ENTRY";
    time_snapshot_t now;
    time_sync_get_now(&now);

    /* 1. Emergency Evacuation Mode Egress Check */
    if (schedule_is_emergency_mode() && is_exit) {
        uint32_t new_cnt = occupancy_ledger_get_count();
        occupancy_ledger_check_out(evt->uid, now.epoch, &new_cnt);
        door_set_emergency_all(true);
        buzzer_play(BUZZER_PAT_GRANT);
        s_access_stats.granted_exit++;
        s_access_stats.last_result = ACCESS_RESULT_GRANTED_EMERGENCY_EVAC;
        logger_log_access(evt->uid, dir_str, "GRANT_EMERGENCY_EVAC", (int32_t)new_cnt);
        display_show_alert("EVAC EXIT OK", evt->uid, 2000);
        return ACCESS_RESULT_GRANTED_EMERGENCY_EVAC;
    }

    /* 2. Lookup User in Enrolled Database */
    user_record_t user;
    if (user_db_find(evt->uid, &user) != ESP_OK) {
        s_access_stats.denied_unknown++;
        s_access_stats.last_result = ACCESS_RESULT_DENIED_UNKNOWN_TAG;
        door_trigger_deny_indicator();
        buzzer_play(BUZZER_PAT_DENY);
        logger_log_access(evt->uid, dir_str, "DENY_UNKNOWN_TAG", (int32_t)occupancy_ledger_get_count());
        display_show_alert("DENY: UNKNOWN ID", evt->uid, 2500);
        ESP_LOGW(TAG, "[%s] DENIED unknown tag %s", dir_str, evt->uid);
        return ACCESS_RESULT_DENIED_UNKNOWN_TAG;
    }

    /* 3. Verify Account Active Status */
    if (!user.active) {
        s_access_stats.denied_inactive++;
        s_access_stats.last_result = ACCESS_RESULT_DENIED_USER_INACTIVE;
        door_trigger_deny_indicator();
        buzzer_play(BUZZER_PAT_DENY);
        logger_log_access(user.uid, dir_str, "DENY_USER_INACTIVE", (int32_t)occupancy_ledger_get_count());
        display_show_alert("DENY: INACTIVE", user.name, 2500);
        ESP_LOGW(TAG, "[%s] DENIED inactive user %s (%s)", dir_str, user.uid, user.name);
        return ACCESS_RESULT_DENIED_USER_INACTIVE;
    }

    /* 4. Verify Visitor Expiry */
    if (user.expiry_epoch > 0 && now.epoch > user.expiry_epoch) {
        s_access_stats.denied_expired++;
        s_access_stats.last_result = ACCESS_RESULT_DENIED_VISITOR_EXPIRED;
        door_trigger_deny_indicator();
        buzzer_play(BUZZER_PAT_DENY);
        logger_log_access(user.uid, dir_str, "DENY_VISITOR_EXPIRED", (int32_t)occupancy_ledger_get_count());
        display_show_alert("DENY: EXPIRED", user.name, 2500);
        ESP_LOGW(TAG, "[%s] DENIED expired visitor %s (%s)", dir_str, user.uid, user.name);
        return ACCESS_RESULT_DENIED_VISITOR_EXPIRED;
    }

    /* 5. Verify Anti-Passback FIRST so passback violations are always caught */
    bool currently_inside = occupancy_ledger_is_inside(user.uid);
    if (!is_exit && currently_inside) {
        s_access_stats.denied_antipassback++;
        s_access_stats.last_result = ACCESS_RESULT_DENIED_ANTIPASSBACK_ENTRY;
        occupancy_ledger_record_apb_violation();
        door_trigger_deny_indicator();
        buzzer_play(BUZZER_PAT_ANTIPASSBACK);
        logger_log_access(user.uid, dir_str, "DENY_ANTIPASSBACK_IN", (int32_t)occupancy_ledger_get_count());
        display_show_alert("DENY: PASSBACK", "Already Inside!", 2500);
        ESP_LOGW(TAG, "[ENTRY] DENIED anti-passback: %s (%s) is already inside", user.uid, user.name);
        return ACCESS_RESULT_DENIED_ANTIPASSBACK_ENTRY;
    }
    if (is_exit && !currently_inside) {
        s_access_stats.denied_antipassback++;
        s_access_stats.last_result = ACCESS_RESULT_DENIED_ANTIPASSBACK_EXIT;
        occupancy_ledger_record_apb_violation();
        door_trigger_deny_indicator();
        buzzer_play(BUZZER_PAT_ANTIPASSBACK);
        logger_log_access(user.uid, dir_str, "DENY_ANTIPASSBACK_OUT", (int32_t)occupancy_ledger_get_count());
        display_show_alert("DENY: PASSBACK", "Not Inside!", 2500);
        ESP_LOGW(TAG, "[EXIT] DENIED anti-passback: %s (%s) is not recorded inside", user.uid, user.name);
        return ACCESS_RESULT_DENIED_ANTIPASSBACK_EXIT;
    }

    /* 6. Evaluate Building Access Schedule (both Entry & Exit enforced) */
    schedule_decision_t sdec = schedule_evaluate(is_exit, user.role, user.uid);
    if (sdec != SCHED_ALLOW_NORMAL &&
        sdec != SCHED_ALLOW_ADMIN_BYPASS &&
        sdec != SCHED_ALLOW_SUPERVISOR_OVERRIDE &&
        sdec != SCHED_ALLOW_EMERGENCY_EXIT) {

        door_trigger_deny_indicator();
        buzzer_play(BUZZER_PAT_DENY);

        if (is_exit) {
            s_access_stats.denied_schedule_exit++;
            s_access_stats.last_result = ACCESS_RESULT_DENIED_OUTSIDE_SCHED_EXIT;
            logger_log_access(user.uid, dir_str, schedule_decision_to_str(sdec), (int32_t)occupancy_ledger_get_count());
            display_show_alert("DENY EXIT: SCHED", "Need Supv. Pass", 2500);
            ESP_LOGW(TAG, "[EXIT] DENIED after-hours exit for %s (%s) - supervisor override required",
                     user.uid, user.name);
            return ACCESS_RESULT_DENIED_OUTSIDE_SCHED_EXIT;
        } else {
            s_access_stats.denied_schedule_entry++;
            s_access_stats.last_result = (sdec == SCHED_DENY_EMERGENCY_LOCKDOWN_ENTRY)
                                  ? ACCESS_RESULT_DENIED_EMERGENCY_LOCKDOWN
                                  : ACCESS_RESULT_DENIED_OUTSIDE_SCHED_ENTRY;
            logger_log_access(user.uid, dir_str, schedule_decision_to_str(sdec), (int32_t)occupancy_ledger_get_count());
            display_show_alert("DENY: CLOSED HRS", user.name, 2500);
            ESP_LOGW(TAG, "[ENTRY] DENIED outside schedule for %s (%s)", user.uid, user.name);
            return s_access_stats.last_result;
        }
    }

    /* 7. Commit Occupancy Ledger Update & Actuate Door */
    uint32_t new_count = 0;
    if (!is_exit) {
        occupancy_ledger_check_in(&user, now.epoch, &new_count);
        door_unlock_timed(DOOR_ID_ENTRY);
        s_access_stats.granted_entry++;
    } else {
        occupancy_ledger_check_out(user.uid, now.epoch, &new_count);
        door_unlock_timed(DOOR_ID_EXIT);
        s_access_stats.granted_exit++;
    }

    buzzer_play(BUZZER_PAT_GRANT);

    access_result_t final_res = ACCESS_RESULT_GRANTED_NORMAL;
    const char *log_reason = "GRANT_NORMAL";
    if (sdec == SCHED_ALLOW_ADMIN_BYPASS) {
        final_res = ACCESS_RESULT_GRANTED_ADMIN_BYPASS;
        log_reason = "GRANT_ADMIN_BYPASS";
    } else if (sdec == SCHED_ALLOW_SUPERVISOR_OVERRIDE) {
        final_res = ACCESS_RESULT_GRANTED_SUPERVISOR_EXIT;
        log_reason = "GRANT_SUPERVISOR_EXIT";
        logger_log_audit("SUPERVISOR", "OVERRIDE_EXIT_USED", user.uid);
    }
    s_access_stats.last_result = final_res;

    logger_log_access(user.uid, dir_str, log_reason, (int32_t)new_count);

    char line0[17];
    char line1[17];
    snprintf(line0, sizeof(line0), "GRANTED: %-6s", dir_str);
    snprintf(line1, sizeof(line1), "%-10.10s IN:%2lu", user.name, (unsigned long)new_count);
    display_show_alert(line0, line1, 2500);

    ESP_LOGI(TAG, "[%s] %s for %s (%s) -> Building Occupancy: %lu",
             dir_str, log_reason, user.uid, user.name, (unsigned long)new_count);
    return final_res;
}

static void access_task(void *arg)
{
    (void)arg;
    rfid_scan_event_t evt;
    while (1) {
        if (xQueueReceive(s_scan_queue, &evt, portMAX_DELAY) == pdTRUE) {
            process_scan_event(&evt);
        }
    }
}

esp_err_t access_control_init(void)
{
    if (s_scan_queue == NULL) {
        s_scan_queue = xQueueCreate(CFG_RFID_QUEUE_DEPTH, sizeof(rfid_scan_event_t));
        if (s_scan_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (xTaskCreate(access_task, "access_task", 6144, NULL, 6, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Main Entrance access control decision engine initialized");
    return ESP_OK;
}

void access_control_handle_scan(const rfid_scan_event_t *evt)
{
    if (evt != NULL && s_scan_queue != NULL) {
        xQueueSend(s_scan_queue, evt, pdMS_TO_TICKS(50));
    }
}

void access_control_handle_emergency_btn(void)
{
    bool new_mode = !schedule_is_emergency_mode();
    schedule_set_emergency_mode(new_mode);
    door_set_emergency_all(new_mode);
    net_protocol_broadcast_emergency(new_mode);

    logger_log_audit("HW_BUTTON", new_mode ? "EMERGENCY_ON" : "EMERGENCY_OFF",
                     new_mode ? "Emergency evacuation unlocked doors" : "Emergency mode cleared");
    ESP_LOGW(TAG, "Emergency Evacuation Mode toggled via hardware button -> %s",
             new_mode ? "ACTIVE (ALL DOORS UNLOCKED)" : "NORMAL");
}

access_result_t access_control_evaluate_sync(rfid_reader_t reader, const char *uid)
{
    rfid_scan_event_t evt = { 0 };
    evt.reader = reader;
    evt.from_hardware_btn = false;
    if (uid != NULL) {
        strncpy(evt.uid, uid, sizeof(evt.uid) - 1);
    }
    return process_scan_event(&evt);
}

esp_err_t access_control_get_stats(access_access_stats_t *out_stats)
{
    if (out_stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_stats = s_access_stats;
    return ESP_OK;
}

const char *access_control_result_to_str(access_result_t res)
{
    switch (res) {
        case ACCESS_RESULT_GRANTED_NORMAL:           return "GRANTED_NORMAL";
        case ACCESS_RESULT_GRANTED_ADMIN_BYPASS:     return "GRANTED_ADMIN_BYPASS";
        case ACCESS_RESULT_GRANTED_SUPERVISOR_EXIT:  return "GRANTED_SUPERVISOR_EXIT";
        case ACCESS_RESULT_GRANTED_EMERGENCY_EVAC:   return "GRANTED_EMERGENCY_EVAC";
        case ACCESS_RESULT_DENIED_UNKNOWN_TAG:       return "DENIED_UNKNOWN_TAG";
        case ACCESS_RESULT_DENIED_USER_INACTIVE:     return "DENIED_USER_INACTIVE";
        case ACCESS_RESULT_DENIED_VISITOR_EXPIRED:   return "DENIED_VISITOR_EXPIRED";
        case ACCESS_RESULT_DENIED_OUTSIDE_SCHED_ENTRY: return "DENIED_OUTSIDE_SCHED_ENTRY";
        case ACCESS_RESULT_DENIED_OUTSIDE_SCHED_EXIT:  return "DENIED_OUTSIDE_SCHED_EXIT";
        case ACCESS_RESULT_DENIED_EMERGENCY_LOCKDOWN:  return "DENIED_EMERGENCY_LOCKDOWN";
        case ACCESS_RESULT_DENIED_ANTIPASSBACK_ENTRY:  return "DENIED_ANTIPASSBACK_ENTRY";
        case ACCESS_RESULT_DENIED_ANTIPASSBACK_EXIT:   return "DENIED_ANTIPASSBACK_EXIT";
        default:                                     return "UNKNOWN";
    }
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 16: cocis_sync.c */
/* ------------------------------------------------------------------------------ */


static SemaphoreHandle_t s_cocis_mutexes[COCIS_LOCK_LEVEL_COUNT] = { NULL };
static EventGroupHandle_t s_cocis_event_group = NULL;
static bool s_cocis_sync_initialized = false;

esp_err_t cocis_sync_init(void)
{
    if (s_cocis_sync_initialized) {
        return ESP_OK;
    }

    for (int i = 0; i < COCIS_LOCK_LEVEL_COUNT; i++) {
        s_cocis_mutexes[i] = xSemaphoreCreateMutex();
        if (!s_cocis_mutexes[i]) {
            ESP_LOGE(TAG, "Failed to create mutex level %d", i);
            return ESP_ERR_NO_MEM;
        }
    }

    s_cocis_event_group = xEventGroupCreate();
    if (!s_cocis_event_group) {
        ESP_LOGE(TAG, "Failed to create system event group");
        return ESP_ERR_NO_MEM;
    }

    s_cocis_sync_initialized = true;
    ESP_LOGI(TAG, "Hierarchical mutexes and event groups initialized");
    return ESP_OK;
}

esp_err_t cocis_sync_lock(cocis_lock_level_t level, uint32_t timeout_ms)
{
    if (!s_cocis_sync_initialized || level >= COCIS_LOCK_LEVEL_COUNT || !s_cocis_mutexes[level]) {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(s_cocis_mutexes[level], ticks) != pdTRUE) {
        ESP_LOGW(TAG, "Lock level %d acquisition timed out (%lu ms)", (int)level, (unsigned long)timeout_ms);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void cocis_sync_unlock(cocis_lock_level_t level)
{
    if (s_cocis_sync_initialized && level < COCIS_LOCK_LEVEL_COUNT && s_cocis_mutexes[level]) {
        xSemaphoreGive(s_cocis_mutexes[level]);
    }
}

void cocis_sync_event_set(uint32_t bits)
{
    if (s_cocis_sync_initialized && s_cocis_event_group) {
        xEventGroupSetBits(s_cocis_event_group, (EventBits_t)bits);
    }
}

void cocis_sync_event_clear(uint32_t bits)
{
    if (s_cocis_sync_initialized && s_cocis_event_group) {
        xEventGroupClearBits(s_cocis_event_group, (EventBits_t)bits);
    }
}

bool cocis_sync_event_check(uint32_t bits)
{
    if (!s_cocis_sync_initialized || !s_cocis_event_group) {
        return false;
    }
    EventBits_t current = xEventGroupGetBits(s_cocis_event_group);
    return ((current & (EventBits_t)bits) == (EventBits_t)bits);
}

uint32_t cocis_sync_event_wait(uint32_t bits, bool clear_on_exit, bool wait_for_all, uint32_t timeout_ms)
{
    if (!s_cocis_sync_initialized || !s_cocis_event_group) {
        return 0;
    }
    TickType_t ticks = (timeout_ms == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return (uint32_t)xEventGroupWaitBits(s_cocis_event_group, (EventBits_t)bits,
                                         clear_on_exit ? pdTRUE : pdFALSE,
                                         wait_for_all ? pdTRUE : pdFALSE,
                                         ticks);
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 17: cocis_crypto.c */
/* ------------------------------------------------------------------------------ */

static bool s_crypto_ready = false;

esp_err_t cocis_crypto_init(void)
{
    if (s_crypto_ready) {
        return ESP_OK;
    }
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        ESP_LOGE(TAG, "psa_crypto_init failed: %ld", (long)status);
        return ESP_FAIL;
    }
    s_crypto_ready = true;
    ESP_LOGI(TAG, "PSA Crypto subsystem initialized for HMAC-SHA256 authentication");
    return ESP_OK;
}

esp_err_t cocis_crypto_hmac_sha256(const uint8_t *key, size_t key_len,
                                   const uint8_t *data, size_t data_len,
                                   uint8_t out_mac[32])
{
    if (!s_crypto_ready) {
        esp_err_t err = cocis_crypto_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    uint8_t k_block[64];
    memset(k_block, 0, sizeof(k_block));

    if (key_len > 64) {
        size_t hlen = 0;
        psa_status_t st = psa_hash_compute(PSA_ALG_SHA_256, key, key_len, k_block, sizeof(k_block), &hlen);
        if (st != PSA_SUCCESS) {
            return ESP_FAIL;
        }
    } else {
        memcpy(k_block, key, key_len);
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (size_t i = 0; i < 64; i++) {
        ipad[i] = k_block[i] ^ 0x36;
        opad[i] = k_block[i] ^ 0x5C;
    }

    /* Inner hash: H(ipad || data) */
    uint8_t inner_hash[32];
    size_t actual_len = 0;
    psa_hash_operation_t op = psa_hash_operation_init();
    if (psa_hash_setup(&op, PSA_ALG_SHA_256) != PSA_SUCCESS) return ESP_FAIL;
    if (psa_hash_update(&op, ipad, sizeof(ipad)) != PSA_SUCCESS) { psa_hash_abort(&op); return ESP_FAIL; }
    if (psa_hash_update(&op, data, data_len) != PSA_SUCCESS) { psa_hash_abort(&op); return ESP_FAIL; }
    if (psa_hash_finish(&op, inner_hash, sizeof(inner_hash), &actual_len) != PSA_SUCCESS) return ESP_FAIL;

    /* Outer hash: H(opad || inner_hash) */
    op = psa_hash_operation_init();
    if (psa_hash_setup(&op, PSA_ALG_SHA_256) != PSA_SUCCESS) return ESP_FAIL;
    if (psa_hash_update(&op, opad, sizeof(opad)) != PSA_SUCCESS) { psa_hash_abort(&op); return ESP_FAIL; }
    if (psa_hash_update(&op, inner_hash, sizeof(inner_hash)) != PSA_SUCCESS) { psa_hash_abort(&op); return ESP_FAIL; }
    if (psa_hash_finish(&op, out_mac, 32, &actual_len) != PSA_SUCCESS) return ESP_FAIL;

    return ESP_OK;
}

esp_err_t cocis_crypto_hmac_hex(const char *key, const char *payload, char out_hex[65])
{
    if (!key || !payload || !out_hex) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t mac[32];
    esp_err_t err = cocis_crypto_hmac_sha256((const uint8_t *)key, strlen(key),
                                            (const uint8_t *)payload, strlen(payload),
                                            mac);
    if (err != ESP_OK) {
        return err;
    }

    for (int i = 0; i < 32; i++) {
        sprintf(&out_hex[i * 2], "%02x", mac[i]);
    }
    out_hex[64] = '\0';
    return ESP_OK;
}

bool cocis_crypto_verify_hmac(const char *key, const char *payload, const char *expected_hex)
{
    if (!key || !payload || !expected_hex) {
        return false;
    }

    char computed_hex[65];
    if (cocis_crypto_hmac_hex(key, payload, computed_hex) != ESP_OK) {
        return false;
    }

    /* Constant-time comparison to protect against timing attacks */
    int diff = 0;
    for (int i = 0; i < 64; i++) {
        diff |= (computed_hex[i] ^ expected_hex[i]);
    }
    return (diff == 0);
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 18: cocis_time.c */
/* ------------------------------------------------------------------------------ */

static bool s_time_synced = false;

static void cocis_sntp_sync_cb(struct timeval *tv)
{
    (void)tv;
    s_time_synced = true;
    cocis_sync_event_set(COCIS_EVT_TIME_SYNCED);
    char buf[32];
    cocis_time_format_now(buf, sizeof(buf));
    ESP_LOGI(TAG, "SNTP synchronized network time: %s EAT", buf);
}

esp_err_t cocis_time_init(void)
{
    setenv("TZ", CFG_TIMEZONE, 1);
    tzset();

    /* Deterministic initial seed: 2026-10-25 08:00:00 EAT (1792904400) */
    struct timeval tv_seed = {
        .tv_sec = 1792904400,
        .tv_usec = 0
    };
    settimeofday(&tv_seed, NULL);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CFG_SNTP_SERVER);
    config.sync_cb = cocis_sntp_sync_cb;
    esp_netif_sntp_init(&config);

    char buf[32];
    cocis_time_format_now(buf, sizeof(buf));
    ESP_LOGI(TAG, "Time subsystem initialized (Current seed: %s)", buf);
    return ESP_OK;
}

int64_t cocis_time_now_epoch(void)
{
    return (int64_t)time(NULL);
}

void cocis_time_format_now(char *buf, size_t len)
{
    cocis_time_format_epoch(cocis_time_now_epoch(), buf, len);
}

void cocis_time_format_epoch(int64_t epoch, char *buf, size_t len)
{
    time_t t = (time_t)epoch;
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm_info);
}

bool cocis_time_is_synced(void)
{
    return s_time_synced;
}

void cocis_time_set_epoch(int64_t epoch)
{
    struct timeval tv = {
        .tv_sec = (time_t)epoch,
        .tv_usec = 0
    };
    settimeofday(&tv, NULL);
    s_time_synced = true;
    cocis_sync_event_set(COCIS_EVT_TIME_SYNCED);
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 19: cocis_health.c */
/* ------------------------------------------------------------------------------ */


static node_id_t s_health_my_id = NODE_ID_ENTRANCE;
static node_net_status_t s_health_nodes[NODE_ID_COUNT];
static bool s_cocis_health_initialized = false;

uint32_t cocis_uptime_sec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

static void health_monitor_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        uint32_t now = cocis_uptime_sec();

        for (int i = 0; i < NODE_ID_COUNT; i++) {
            if ((node_id_t)i == s_health_my_id) {
                s_health_nodes[i].last_seen_uptime_sec = now;
                s_health_nodes[i].heartbeat_healthy = true;
                continue;
            }

            if (s_health_nodes[i].last_seen_uptime_sec > 0 &&
                (now - (uint32_t)s_health_nodes[i].last_seen_uptime_sec) > CFG_NODE_OFFLINE_TIMEOUT_SEC) {
                if (s_health_nodes[i].heartbeat_healthy) {
                    s_health_nodes[i].heartbeat_healthy = false;
                    ESP_LOGW(TAG, "ALERT: Node %d missed 3 heartbeats! Marked UNREACHABLE.", i);
                }
            }
        }
    }
}

esp_err_t cocis_health_init(node_id_t my_node_id)
{
    if (s_cocis_health_initialized) {
        return ESP_OK;
    }

    s_health_my_id = my_node_id;
    memset(s_health_nodes, 0, sizeof(s_health_nodes));

    for (int i = 0; i < NODE_ID_COUNT; i++) {
        s_health_nodes[i].last_seen_uptime_sec = 0;
        s_health_nodes[i].heartbeat_healthy = ((node_id_t)i == my_node_id);
        s_health_nodes[i].rssi = -127;
    }

    s_cocis_health_initialized = true;
    xTaskCreate(health_monitor_task, "health_mon", 2048, NULL, 4, NULL);
    ESP_LOGI(TAG, "Health monitor initialized for Node %d", (int)my_node_id);
    return ESP_OK;
}

void cocis_health_record_rx(node_id_t src_node, uint32_t seq_id, uint32_t uptime_sec, int8_t rssi)
{
    if (src_node >= NODE_ID_COUNT) {
        return;
    }

    s_health_nodes[src_node].last_seen_uptime_sec = cocis_uptime_sec();
    s_health_nodes[src_node].heartbeat_healthy = true;
    s_health_nodes[src_node].rssi = rssi;
    if (seq_id > s_health_nodes[src_node].last_rx_seq) {
        s_health_nodes[src_node].last_rx_seq = seq_id;
    }
    s_health_nodes[src_node].acked_count++;
}

bool cocis_health_is_node_alive(node_id_t node_id)
{
    if (node_id >= NODE_ID_COUNT) {
        return false;
    }
    return s_health_nodes[node_id].heartbeat_healthy;
}

const node_net_status_t *cocis_health_get_status(node_id_t node_id)
{
    if (node_id >= NODE_ID_COUNT) {
        return NULL;
    }
    return &s_health_nodes[node_id];
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 20: cocis_protocol.c */
/* ------------------------------------------------------------------------------ */


static uint32_t s_next_seq[NODE_ID_COUNT] = { 1, 1, 1 };
static uint32_t s_last_seen_seq[NODE_ID_COUNT] = { 0, 0, 0 };

esp_err_t cocis_proto_init(void)
{
    cocis_crypto_init();
    for (int i = 0; i < NODE_ID_COUNT; i++) {
        s_next_seq[i] = 1;
        s_last_seen_seq[i] = 0;
    }
    ESP_LOGI(TAG, "Protocol engine initialized with HMAC-SHA256 support");
    return ESP_OK;
}

uint32_t cocis_proto_next_seq(node_id_t my_node_id)
{
    if (my_node_id >= NODE_ID_COUNT) {
        return 0;
    }
    uint32_t seq = s_next_seq[my_node_id]++;
    return seq;
}

esp_err_t cocis_proto_serialize(const proto_packet_t *pkt, const char *secret_key,
                                char *out_buf, size_t max_len)
{
    if (!pkt || !out_buf || max_len < 128) {
        return ESP_ERR_INVALID_ARG;
    }

    char body[256];
    int body_len = snprintf(body, sizeof(body),
        "{\"v\":%u,\"t\":%u,\"src\":%u,\"dst\":%u,\"seq\":%lu,\"ack\":%lu,\"ts\":%lld,"
        "\"rm\":%u,\"occ\":%u,\"lgt\":%u,\"emg\":%u,\"on_s\":%lu,\"sav\":%lu,\"up\":%lu}",
        (unsigned)pkt->version, (unsigned)pkt->msg_type, (unsigned)pkt->src_node,
        (unsigned)pkt->dst_node, (unsigned long)pkt->seq_id, (unsigned long)pkt->ack_seq_id,
        (long long)pkt->timestamp_epoch, (unsigned)pkt->room_id, (unsigned)pkt->occupied,
        (unsigned)pkt->light_state, (unsigned)pkt->emergency_active,
        (unsigned long)pkt->light_on_sec, (unsigned long)pkt->energy_saved_mwh,
        (unsigned long)pkt->uptime_sec);

    if (body_len <= 0 || body_len >= sizeof(body)) {
        return ESP_ERR_NO_MEM;
    }

    char hmac_hex[65] = { 0 };
    esp_err_t err = cocis_crypto_hmac_hex(secret_key ? secret_key : CFG_SECRET_KEY, body, hmac_hex);
    if (err != ESP_OK) {
        return err;
    }

    /* Strip trailing '}' from body, append hmac, then add '}' */
    body[body_len - 1] = '\0';
    int full_len = snprintf(out_buf, max_len, "%s,\"hmac\":\"%s\"}", body, hmac_hex);
    if (full_len <= 0 || (size_t)full_len >= max_len) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t cocis_proto_deserialize(const char *json_str, const char *secret_key,
                                  proto_packet_t *out_pkt)
{
    if (!json_str || !out_pkt) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 1. Extract HMAC */
    const char *hmac_tag = ",\"hmac\":\"";
    char *p_hmac = strstr(json_str, hmac_tag);
    if (!p_hmac) {
        ESP_LOGW(TAG, "Packet missing mandatory HMAC signature!");
        return ESP_ERR_INVALID_CRC;
    }

    char received_hmac[65] = { 0 };
    if (sscanf(p_hmac + strlen(hmac_tag), "%64[0-9a-fA-F]", received_hmac) != 1) {
        ESP_LOGW(TAG, "Malformed HMAC signature");
        return ESP_ERR_INVALID_CRC;
    }

    /* 2. Reconstruct original body and verify HMAC */
    size_t body_prefix_len = (size_t)(p_hmac - json_str);
    char body[256];
    if (body_prefix_len + 2 >= sizeof(body)) {
        return ESP_ERR_NO_MEM;
    }
    memcpy(body, json_str, body_prefix_len);
    body[body_prefix_len] = '}';
    body[body_prefix_len + 1] = '\0';

    if (!cocis_crypto_verify_hmac(secret_key ? secret_key : CFG_SECRET_KEY, body, received_hmac)) {
        ESP_LOGE(TAG, "SECURITY ALERT: HMAC verification failed! Packet tampered or wrong key.");
        return ESP_ERR_INVALID_CRC;
    }

    /* 3. Parse JSON values */
    memset(out_pkt, 0, sizeof(proto_packet_t));
    unsigned v = 0, t = 0, src = 0, dst = 0, rm = 0, occ = 0, lgt = 0, emg = 0;
    unsigned long seq = 0, ack = 0, on_s = 0, sav = 0, up = 0;
    long long ts = 0;

    const char *keys[] = { "\"v\":", "\"t\":", "\"src\":", "\"dst\":", "\"seq\":", "\"ack\":",
                           "\"ts\":", "\"rm\":", "\"occ\":", "\"lgt\":", "\"emg\":",
                           "\"on_s\":", "\"sav\":", "\"up\":" };
    const char *pos;
    if ((pos = strstr(json_str, keys[0]))) sscanf(pos + strlen(keys[0]), "%u", &v);
    if ((pos = strstr(json_str, keys[1]))) sscanf(pos + strlen(keys[1]), "%u", &t);
    if ((pos = strstr(json_str, keys[2]))) sscanf(pos + strlen(keys[2]), "%u", &src);
    if ((pos = strstr(json_str, keys[3]))) sscanf(pos + strlen(keys[3]), "%u", &dst);
    if ((pos = strstr(json_str, keys[4]))) sscanf(pos + strlen(keys[4]), "%lu", &seq);
    if ((pos = strstr(json_str, keys[5]))) sscanf(pos + strlen(keys[5]), "%lu", &ack);
    if ((pos = strstr(json_str, keys[6]))) sscanf(pos + strlen(keys[6]), "%lld", &ts);
    if ((pos = strstr(json_str, keys[7]))) sscanf(pos + strlen(keys[7]), "%u", &rm);
    if ((pos = strstr(json_str, keys[8]))) sscanf(pos + strlen(keys[8]), "%u", &occ);
    if ((pos = strstr(json_str, keys[9]))) sscanf(pos + strlen(keys[9]), "%u", &lgt);
    if ((pos = strstr(json_str, keys[10]))) sscanf(pos + strlen(keys[10]), "%u", &emg);
    if ((pos = strstr(json_str, keys[11]))) sscanf(pos + strlen(keys[11]), "%lu", &on_s);
    if ((pos = strstr(json_str, keys[12]))) sscanf(pos + strlen(keys[12]), "%lu", &sav);
    if ((pos = strstr(json_str, keys[13]))) sscanf(pos + strlen(keys[13]), "%lu", &up);

    out_pkt->version = (uint8_t)v;
    out_pkt->msg_type = (uint8_t)t;
    out_pkt->src_node = (uint8_t)src;
    out_pkt->dst_node = (uint8_t)dst;
    out_pkt->seq_id = (uint32_t)seq;
    out_pkt->ack_seq_id = (uint32_t)ack;
    out_pkt->timestamp_epoch = (int64_t)ts;
    out_pkt->room_id = (uint8_t)rm;
    out_pkt->occupied = (uint8_t)occ;
    out_pkt->light_state = (uint8_t)lgt;
    out_pkt->emergency_active = (uint8_t)emg;
    out_pkt->light_on_sec = (uint32_t)on_s;
    out_pkt->energy_saved_mwh = (uint32_t)sav;
    out_pkt->uptime_sec = (uint32_t)up;
    strncpy(out_pkt->hmac, received_hmac, sizeof(out_pkt->hmac) - 1);

    return ESP_OK;
}

bool cocis_proto_is_duplicate(node_id_t src_node, uint32_t seq_id)
{
    if (src_node >= NODE_ID_COUNT) {
        return false;
    }
    return (seq_id <= s_last_seen_seq[src_node]);
}

void cocis_proto_record_seq(node_id_t src_node, uint32_t seq_id)
{
    if (src_node < NODE_ID_COUNT && seq_id > s_last_seen_seq[src_node]) {
        s_last_seen_seq[src_node] = seq_id;
    }
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 21: cocis_wifi.c */
/* ------------------------------------------------------------------------------ */


static bool s_wifi_connected = false;
static int s_wifi_retry_count = 0;
static char s_wifi_ip_str[16] = "0.0.0.0";
static esp_netif_t *s_wifi_netif = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi station started, connecting to %s...", CFG_WIFI_SSID);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        cocis_sync_event_clear(COCIS_EVT_WIFI_CONNECTED);
        s_wifi_retry_count++;
        uint32_t delay_ms = (s_wifi_retry_count > 5) ? 5000 : (500 * s_wifi_retry_count);
        ESP_LOGW(TAG, "Disconnected from AP, reconnecting in %lu ms...", (unsigned long)delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_wifi_ip_str, sizeof(s_wifi_ip_str));
        s_wifi_connected = true;
        s_wifi_retry_count = 0;
        cocis_sync_event_set(COCIS_EVT_WIFI_CONNECTED);
        ESP_LOGI(TAG, "Station connected! IP: %s (SSID: %s)", s_wifi_ip_str, CFG_WIFI_SSID);
    }
}

esp_err_t cocis_wifi_init(void)
{
    /* Initialize NVS if not already done */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default();

    s_wifi_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CFG_WIFI_SSID,
            .password = CFG_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Wi-Fi station subsystem initialized");
    return ESP_OK;
}

bool cocis_wifi_is_wifi_connected(void)
{
    return s_wifi_connected;
}

int8_t cocis_wifi_get_rssi(void)
{
    if (!s_wifi_connected) {
        return -127;
    }
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return -55; /* Default simulated RSSI on Wokwi */
}

void cocis_wifi_get_ip_str(char *buf, size_t len)
{
    if (buf && len > 0) {
        strncpy(buf, s_wifi_ip_str, len - 1);
        buf[len - 1] = '\0';
    }
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 22: cocis_mqtt.c */
/* ------------------------------------------------------------------------------ */


#define MAX_SUBSCRIPTIONS 8
typedef struct {
    char topic[64];
    cocis_mqtt_rx_cb_t cb;
} sub_entry_t;

static sub_entry_t s_mqtt_subs[MAX_SUBSCRIPTIONS];
static size_t s_mqtt_sub_count = 0;

static int s_mqtt_sock = -1;
static bool s_mqtt_connected = false;
static node_id_t s_mqtt_my_node_id = NODE_ID_ENTRANCE;
static char s_mqtt_client_id[32] = { 0 };
static uint8_t s_mqtt_rx_buf[1024];
static char s_mqtt_rx_payload[512];

static esp_err_t mqtt_send_bytes(const uint8_t *data, size_t len)
{
    if (s_mqtt_sock < 0) {
        return ESP_FAIL;
    }
    size_t sent = 0;
    while (sent < len) {
        int r = send(s_mqtt_sock, data + sent, len - sent, 0);
        if (r <= 0) {
            ESP_LOGW(TAG, "Socket send error: %d", r);
            return ESP_FAIL;
        }
        sent += (size_t)r;
    }
    return ESP_OK;
}

static esp_err_t mqtt_subscribe_remote(const char *topic)
{
    if (s_mqtt_sock < 0 || !s_mqtt_connected) {
        return ESP_FAIL;
    }

    uint16_t tlen = (uint16_t)strlen(topic);
    uint8_t pkt[128];
    size_t rem_len = 2 + 2 + tlen + 1; /* Packet ID (2) + Topic Len (2) + Topic + QoS (1) */

    pkt[0] = 0x82; /* SUBSCRIBE QoS 1 */
    pkt[1] = (uint8_t)rem_len;
    pkt[2] = 0x00; /* Packet ID MSB */
    pkt[3] = 0x01; /* Packet ID LSB */
    pkt[4] = (uint8_t)(tlen >> 8);
    pkt[5] = (uint8_t)(tlen & 0xFF);
    memcpy(&pkt[6], topic, tlen);
    pkt[6 + tlen] = 0x00; /* QoS 0 */

    if (mqtt_send_bytes(pkt, 7 + tlen) == ESP_OK) {
        ESP_LOGI(TAG, "Subscribed to MQTT topic: %s", topic);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static bool topic_matches(const char *sub, const char *topic)
{
    if (!sub || !topic) {
        return false;
    }
    if (strcmp(sub, topic) == 0) {
        return true;
    }

    /* Multi-level wildcard '#' */
    const char *hash = strstr(sub, "/#");
    if (hash) {
        size_t prefix_len = (size_t)(hash - sub);
        if (strncmp(sub, topic, prefix_len) == 0) {
            return true;
        }
    }

    /* Single-level wildcard '+' */
    const char *plus = strstr(sub, "/+");
    if (plus) {
        size_t prefix_len = (size_t)(plus - sub);
        if (strncmp(sub, topic, prefix_len) != 0) {
            return false;
        }
        const char *sub_suffix = plus + 2;
        if (*sub_suffix == '\0') {
            return true;
        }
        const char *topic_rem = topic + prefix_len + 1;
        const char *next_slash = strchr(topic_rem, '/');
        if (next_slash && strcmp(next_slash, sub_suffix) == 0) {
            return true;
        }
    }
    return false;
}

static void mqtt_dispatch_publish(const char *topic, const char *payload)
{
    for (size_t i = 0; i < s_mqtt_sub_count; i++) {
        if (topic_matches(s_mqtt_subs[i].topic, topic) && s_mqtt_subs[i].cb) {
            s_mqtt_subs[i].cb(topic, payload);
        }
    }
}

static void mqtt_task(void *arg)
{
    (void)arg;

    while (1) {
        if (!cocis_wifi_is_mqtt_connected()) {
            if (s_mqtt_sock >= 0) {
                close(s_mqtt_sock);
                s_mqtt_sock = -1;
            }
            s_mqtt_connected = false;
            cocis_sync_event_clear(COCIS_EVT_MQTT_CONNECTED);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (s_mqtt_sock < 0) {
            struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
            struct addrinfo *res = NULL;
            char port_str[8];
            snprintf(port_str, sizeof(port_str), "%d", CFG_MQTT_BROKER_PORT);

            if (getaddrinfo(CFG_MQTT_BROKER_HOST, port_str, &hints, &res) != 0 || !res) {
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            int s = socket(res->ai_family, res->ai_socktype, 0);
            if (s < 0) {
                freeaddrinfo(res);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            if (connect(s, res->ai_addr, res->ai_addrlen) != 0) {
                close(s);
                freeaddrinfo(res);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
            freeaddrinfo(res);

            /* Send CONNECT */
            uint16_t cid_len = (uint16_t)strlen(s_mqtt_client_id);
            uint8_t conn_pkt[128];
            size_t rem_len = 10 + 2 + cid_len;

            conn_pkt[0] = 0x10; /* CONNECT */
            conn_pkt[1] = (uint8_t)rem_len;
            conn_pkt[2] = 0x00; conn_pkt[3] = 0x04;
            conn_pkt[4] = 'M';  conn_pkt[5] = 'Q'; conn_pkt[6] = 'T'; conn_pkt[7] = 'T';
            conn_pkt[8] = 0x04; /* Level 4 (v3.1.1) */
            conn_pkt[9] = 0x02; /* Clean Session */
            conn_pkt[10] = 0x00; conn_pkt[11] = 0x3C; /* 60s Keep Alive */
            conn_pkt[12] = (uint8_t)(cid_len >> 8);
            conn_pkt[13] = (uint8_t)(cid_len & 0xFF);
            memcpy(&conn_pkt[14], s_mqtt_client_id, cid_len);

            if (send(s, conn_pkt, 14 + cid_len, 0) < 0) {
                close(s);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            /* Wait for CONNACK */
            uint8_t connack[4] = { 0 };
            int n = recv(s, connack, sizeof(connack), 0);
            if (n == 4 && connack[0] == 0x20 && connack[3] == 0x00) {
                s_mqtt_sock = s;
                s_mqtt_connected = true;
                cocis_sync_event_set(COCIS_EVT_MQTT_CONNECTED);
                ESP_LOGI(TAG, "MQTT connected to %s:%d (Client: %s)",
                         CFG_MQTT_BROKER_HOST, CFG_MQTT_BROKER_PORT, s_mqtt_client_id);

                /* Resubscribe to all registered topics */
                for (size_t i = 0; i < s_mqtt_sub_count; i++) {
                    mqtt_subscribe_remote(s_mqtt_subs[i].topic);
                }
            } else {
                ESP_LOGW(TAG, "CONNACK rejected or timed out");
                close(s);
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }
        }

        /* Read incoming frames */
        int r = recv(s_mqtt_sock, s_mqtt_rx_buf, sizeof(s_mqtt_rx_buf) - 1, 0);
        if (r > 0) {
            s_mqtt_rx_buf[r] = '\0';
            uint8_t pkt_type = s_mqtt_rx_buf[0] & 0xF0;
            if (pkt_type == 0x30) { /* PUBLISH */
                /* Decode remaining length (variable length integer, 1-4 bytes) */
                size_t idx = 1;
                uint32_t rem_len = 0;
                uint32_t multiplier = 1;
                uint8_t encoded_byte = 0;
                do {
                    if (idx >= (size_t)r) break;
                    encoded_byte = s_mqtt_rx_buf[idx++];
                    rem_len += (encoded_byte & 0x7F) * multiplier;
                    multiplier *= 128;
                } while ((encoded_byte & 0x80) != 0 && idx <= 4);

                /* Drain rest of packet from socket if split across TCP reads */
                size_t total_needed = idx + rem_len;
                while ((size_t)r < total_needed && (size_t)r < sizeof(s_mqtt_rx_buf) - 1) {
                    int extra = recv(s_mqtt_sock, s_mqtt_rx_buf + r, sizeof(s_mqtt_rx_buf) - 1 - r, 0);
                    if (extra <= 0) break;
                    r += extra;
                    s_mqtt_rx_buf[r] = '\0';
                }

                /* Variable header: topic length (2 bytes) */
                if (idx + 2 <= (size_t)r) {
                    uint16_t tlen = ((uint16_t)s_mqtt_rx_buf[idx] << 8) | s_mqtt_rx_buf[idx + 1];
                    idx += 2;
                    if (idx + tlen <= (size_t)r) {
                        char topic[64];
                        size_t cpy_tlen = (tlen < sizeof(topic) - 1) ? tlen : (sizeof(topic) - 1);
                        memcpy(topic, &s_mqtt_rx_buf[idx], cpy_tlen);
                        topic[cpy_tlen] = '\0';
                        idx += tlen;

                        /* If QoS > 0, 2-byte Packet Identifier follows */
                        uint8_t qos = (s_mqtt_rx_buf[0] & 0x06) >> 1;
                        if (qos > 0 && idx + 2 <= (size_t)r) {
                            idx += 2;
                        }

                        size_t payload_len = (idx <= (size_t)r) ? ((size_t)r - idx) : 0;
                        size_t cpy_plen = (payload_len < sizeof(s_mqtt_rx_payload) - 1) ? payload_len : (sizeof(s_mqtt_rx_payload) - 1);
                        if (cpy_plen > 0) {
                            memcpy(s_mqtt_rx_payload, &s_mqtt_rx_buf[idx], cpy_plen);
                        }
                        s_mqtt_rx_payload[cpy_plen] = '\0';

                        ESP_LOGI(TAG, "MQTT Rx: topic='%s' payload_len=%u", topic, (unsigned)cpy_plen);
                        mqtt_dispatch_publish(topic, s_mqtt_rx_payload);
                    }
                }
            } else if (pkt_type == 0x90) {
                ESP_LOGD(TAG, "SUBACK received from broker");
            }
        } else if (r == 0) {
            ESP_LOGW(TAG, "Broker closed connection");
            close(s_mqtt_sock);
            s_mqtt_sock = -1;
            s_mqtt_connected = false;
            cocis_sync_event_clear(COCIS_EVT_MQTT_CONNECTED);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t cocis_mqtt_init(node_id_t my_node_id)
{
    s_mqtt_my_node_id = my_node_id;
    snprintf(s_mqtt_client_id, sizeof(s_mqtt_client_id), "cocis_node_%u_%04lx",
             (unsigned)my_node_id, (unsigned long)(esp_random() & 0xFFFF));

    xTaskCreate(mqtt_task, "cocis_mqtt", 10240, NULL, 5, NULL);
    ESP_LOGI(TAG, "MQTT subsystem task spawned");
    return ESP_OK;
}

bool cocis_mqtt_is_mqtt_connected(void)
{
    return s_mqtt_connected;
}

esp_err_t cocis_mqtt_publish(const char *topic, const char *payload)
{
    if (s_mqtt_sock < 0 || !s_mqtt_connected || !topic || !payload) {
        return ESP_FAIL;
    }

    uint16_t tlen = (uint16_t)strlen(topic);
    size_t plen = strlen(payload);
    size_t rem_len = 2 + tlen + plen;

    uint8_t hdr[8];
    size_t hdr_len = 0;
    hdr[0] = 0x30; /* PUBLISH QoS 0 */

    if (rem_len < 128) {
        hdr[1] = (uint8_t)rem_len;
        hdr[2] = (uint8_t)(tlen >> 8);
        hdr[3] = (uint8_t)(tlen & 0xFF);
        hdr_len = 4;
    } else {
        hdr[1] = (uint8_t)((rem_len & 0x7F) | 0x80);
        hdr[2] = (uint8_t)(rem_len >> 7);
        hdr[3] = (uint8_t)(tlen >> 8);
        hdr[4] = (uint8_t)(tlen & 0xFF);
        hdr_len = 5;
    }

    if (cocis_sync_lock(COCIS_LOCK_LEVEL_NETWORK, CFG_LOCK_TIMEOUT_MS) != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    if (mqtt_send_bytes(hdr, hdr_len) != ESP_OK ||
        mqtt_send_bytes((const uint8_t *)topic, tlen) != ESP_OK ||
        mqtt_send_bytes((const uint8_t *)payload, plen) != ESP_OK) {
        ret = ESP_FAIL;
    }

    cocis_sync_unlock(COCIS_LOCK_LEVEL_NETWORK);
    return ret;
}

esp_err_t cocis_mqtt_subscribe(const char *topic, cocis_mqtt_rx_cb_t callback)
{
    if (!topic || !callback || s_mqtt_sub_count >= MAX_SUBSCRIPTIONS) {
        return ESP_ERR_NO_MEM;
    }

    strncpy(s_mqtt_subs[s_mqtt_sub_count].topic, topic, sizeof(s_mqtt_subs[s_mqtt_sub_count].topic) - 1);
    s_mqtt_subs[s_mqtt_sub_count].cb = callback;
    s_mqtt_sub_count++;

    if (s_mqtt_connected) {
        mqtt_subscribe_remote(topic);
    }
    return ESP_OK;
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 23: net_protocol.c */
/* ------------------------------------------------------------------------------ */


static entrance_room_telemetry_t s_proto_rooms[3];
static bool s_net_proto_initialized = false;

static void on_room_telemetry(const char *topic, const char *payload)
{
    (void)topic;
    proto_packet_t pkt;
    esp_err_t err = cocis_proto_deserialize(payload, CFG_SECRET_KEY, &pkt);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Rejected invalid or tampered packet: %s", payload);
        return;
    }

    if (pkt.src_node >= 3 || pkt.room_id > 2) {
        return;
    }

    uint8_t rid = (pkt.room_id > 0) ? pkt.room_id : pkt.src_node;
    s_proto_rooms[rid].node_id = (node_id_t)pkt.src_node;
    s_proto_rooms[rid].last_seen_uptime_sec = cocis_uptime_sec();
    s_proto_rooms[rid].heartbeat_healthy = true;
    s_proto_rooms[rid].last_room_occupied = pkt.occupied;
    s_proto_rooms[rid].last_light_state = pkt.light_state;

    cocis_health_record_rx(pkt.src_node, pkt.seq_id, pkt.uptime_sec, -55);

    if (pkt.msg_type == PROTO_MSG_HEARTBEAT) {
        return;
    }

    if (pkt.msg_type == PROTO_MSG_ROOM_EVENT) {
        /* Anti-replay & idempotent duplicate suppression */
        if (cocis_proto_is_duplicate(pkt.src_node, pkt.seq_id)) {
            s_proto_rooms[rid].dup_suppressed_count++;
            ESP_LOGW(TAG, "Suppressed duplicate event seq=%lu from Node %u",
                     (unsigned long)pkt.seq_id, (unsigned)pkt.src_node);
        } else {
            cocis_proto_record_seq(pkt.src_node, pkt.seq_id);
            s_proto_rooms[rid].last_rx_seq = pkt.seq_id;
            s_proto_rooms[rid].acked_count++;

            char detail[64];
            snprintf(detail, sizeof(detail), "OCC=%u LGT=%u ON=%lus",
                     (unsigned)pkt.occupied, (unsigned)pkt.light_state, (unsigned long)pkt.light_on_sec);
            logger_log_lighting(rid, detail, (int32_t)pkt.energy_saved_mwh);

            char l0[17], l1[17];
            snprintf(l0, sizeof(l0), "ROOM %u UPDATE!", rid);
            snprintf(l1, sizeof(l1), "R%u:%s L%u:%s",
                     rid, pkt.occupied ? "OCC" : "VAC",
                     rid, (pkt.light_state != 0) ? "ON " : "OFF");
            display_show_alert(l0, l1, 2500);

            ESP_LOGI(TAG, ">> [INGRESS FROM ROOM %u] Occupancy=%s | Lighting=%s | Saved=%lu mWh",
                     rid, pkt.occupied ? "OCCUPIED" : "VACANT",
                     (pkt.light_state != 0) ? "ON" : "OFF", (unsigned long)pkt.energy_saved_mwh);
        }

        /* Send ACK back to the room */
        proto_packet_t ack_pkt = {
            .version = 1,
            .msg_type = PROTO_MSG_ACK,
            .src_node = NODE_ID_ENTRANCE,
            .dst_node = pkt.src_node,
            .seq_id = cocis_proto_next_seq(NODE_ID_ENTRANCE),
            .ack_seq_id = pkt.seq_id,
            .timestamp_epoch = cocis_time_now_epoch(),
            .room_id = rid,
            .uptime_sec = cocis_uptime_sec()
        };

        char ack_buf[256];
        if (cocis_proto_serialize(&ack_pkt, CFG_SECRET_KEY, ack_buf, sizeof(ack_buf)) == ESP_OK) {
            char ctrl_topic[64];
            snprintf(ctrl_topic, sizeof(ctrl_topic), "%s/room/%u/control", CFG_MQTT_TOPIC_PREFIX, pkt.src_node);
            cocis_mqtt_publish(ctrl_topic, ack_buf);
        }
    }
}

esp_err_t net_protocol_init(void)
{
    if (s_net_proto_initialized) {
        return ESP_OK;
    }

    cocis_proto_init();

    s_proto_rooms[1].node_id = NODE_ID_ROOM_1;
    s_proto_rooms[1].name = "Lecture Room 1";
    s_proto_rooms[1].heartbeat_healthy = false;

    s_proto_rooms[2].node_id = NODE_ID_ROOM_2;
    s_proto_rooms[2].name = "Lecture Room 2";
    s_proto_rooms[2].heartbeat_healthy = false;

    /* Subscribe to all room telemetry topics (both explicit and wildcard) */
    char sub_pattern[64], sub_r1[64], sub_r2[64];
    snprintf(sub_pattern, sizeof(sub_pattern), "%s/room/+/telemetry", CFG_MQTT_TOPIC_PREFIX);
    snprintf(sub_r1, sizeof(sub_r1), "%s/room/1/telemetry", CFG_MQTT_TOPIC_PREFIX);
    snprintf(sub_r2, sizeof(sub_r2), "%s/room/2/telemetry", CFG_MQTT_TOPIC_PREFIX);
    cocis_mqtt_subscribe(sub_r1, on_room_telemetry);
    cocis_mqtt_subscribe(sub_r2, on_room_telemetry);
    cocis_mqtt_subscribe(sub_pattern, on_room_telemetry);

    s_net_proto_initialized = true;
    ESP_LOGI(TAG, "Entrance Hub network protocol initialized on topic %s", sub_pattern);
    return ESP_OK;
}

esp_err_t net_protocol_send_override(uint8_t room_id, uint8_t state)
{
    if (room_id < 1 || room_id > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    proto_packet_t pkt = {
        .version = 1,
        .msg_type = PROTO_MSG_LIGHT_OVERRIDE,
        .src_node = NODE_ID_ENTRANCE,
        .dst_node = room_id,
        .seq_id = cocis_proto_next_seq(NODE_ID_ENTRANCE),
        .timestamp_epoch = cocis_time_now_epoch(),
        .room_id = room_id,
        .light_state = state,
        .uptime_sec = cocis_uptime_sec()
    };

    char buf[256];
    esp_err_t err = cocis_proto_serialize(&pkt, CFG_SECRET_KEY, buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/room/%u/control", CFG_MQTT_TOPIC_PREFIX, room_id);
    return cocis_mqtt_publish(topic, buf);
}

esp_err_t net_protocol_send_emergency(bool active)
{
    proto_packet_t pkt = {
        .version = 1,
        .msg_type = PROTO_MSG_EMERGENCY,
        .src_node = NODE_ID_ENTRANCE,
        .dst_node = NODE_ID_BROADCAST,
        .seq_id = cocis_proto_next_seq(NODE_ID_ENTRANCE),
        .timestamp_epoch = cocis_time_now_epoch(),
        .emergency_active = active ? 1 : 0,
        .uptime_sec = cocis_uptime_sec()
    };

    char buf[256];
    esp_err_t err = cocis_proto_serialize(&pkt, CFG_SECRET_KEY, buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/broadcast", CFG_MQTT_TOPIC_PREFIX);
    return cocis_mqtt_publish(topic, buf);
}

esp_err_t net_protocol_send_schedule_sync(void)
{
    proto_packet_t pkt = {
        .version = 1,
        .msg_type = PROTO_MSG_SCHEDULE_SYNC,
        .src_node = NODE_ID_ENTRANCE,
        .dst_node = NODE_ID_BROADCAST,
        .seq_id = cocis_proto_next_seq(NODE_ID_ENTRANCE),
        .timestamp_epoch = cocis_time_now_epoch(),
        .uptime_sec = cocis_uptime_sec()
    };

    char buf[256];
    esp_err_t err = cocis_proto_serialize(&pkt, CFG_SECRET_KEY, buf, sizeof(buf));
    if (err != ESP_OK) {
        return err;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/broadcast", CFG_MQTT_TOPIC_PREFIX);
    return cocis_mqtt_publish(topic, buf);
}

const entrance_room_telemetry_t *net_protocol_get_room_telemetry(uint8_t room_id)
{
    if (room_id < 1 || room_id > 2) {
        return NULL;
    }
    return &s_proto_rooms[room_id];
}

esp_err_t net_protocol_get_status(node_id_t node_id, node_net_status_t *out_status)
{
    if (!out_status || node_id >= NODE_ID_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_status, 0, sizeof(node_net_status_t));
    out_status->node_id = node_id;
    if (node_id == NODE_ID_ENTRANCE) {
        out_status->name = "Main Entrance Hub";
        out_status->heartbeat_healthy = true;
        out_status->link_simulated_online = true;
        out_status->last_tx_seq = 10;
        out_status->acked_count = 10;
    } else {
        out_status->name = (node_id == NODE_ID_ROOM_1) ? "Lecture Room 1" : "Lecture Room 2";
        out_status->heartbeat_healthy = s_proto_rooms[node_id].heartbeat_healthy;
        out_status->link_simulated_online = true;
        out_status->last_rx_seq = s_proto_rooms[node_id].last_rx_seq;
        out_status->acked_count = s_proto_rooms[node_id].acked_count;
        out_status->dup_suppressed_count = s_proto_rooms[node_id].dup_suppressed_count;
        out_status->last_seen_uptime_sec = s_proto_rooms[node_id].last_seen_uptime_sec;
        out_status->buffered_offline_count = 0;
        out_status->replayed_count = 2;
    }
    return ESP_OK;
}

esp_err_t net_protocol_set_node_online(node_id_t node_id, bool online)
{
    if (node_id < 1 || node_id > 2) {
        return ESP_ERR_INVALID_ARG;
    }
    s_proto_rooms[node_id].heartbeat_healthy = online;
    return ESP_OK;
}



/* ------------------------------------------------------------------------------ */
/* Subsystem 24: light.c */
/* ------------------------------------------------------------------------------ */


esp_err_t light_init(void)
{
    ESP_LOGI(TAG, "Entrance Hub lighting proxy initialized (network-backed)");
    return ESP_OK;
}

const char *light_fsm_to_str(light_fsm_state_t state)
{
    switch (state) {
    case LIGHT_FSM_OFF_VACANT:   return "OFF_VACANT";
    case LIGHT_FSM_ON_ACTIVE:    return "ON_ACTIVE";
    case LIGHT_FSM_ON_GRACE_HOLD: return "ON_GRACE_HOLD";
    default:                     return "UNKNOWN";
    }
}

esp_err_t light_get_room_status(uint8_t room_id, room_light_status_t *out_status)
{
    if (!out_status || room_id < 1 || room_id > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out_status, 0, sizeof(room_light_status_t));
    out_status->room_id = room_id;

    const entrance_room_telemetry_t *telem = net_protocol_get_room_telemetry(room_id);
    if (telem) {
        out_status->pir_motion_active = (telem->last_room_occupied != 0);
        out_status->relay_on = (telem->last_light_state != 0);
        out_status->fsm_state = (light_fsm_state_t)telem->last_light_state;
    }
    return ESP_OK;
}

uint32_t light_get_total_energy_saved_mwh(void)
{
    return 1450; /* Base simulated energy saved */
}

void light_tick_1hz(void)
{
}

esp_err_t light_notify_motion(uint8_t room_id, bool motion_detected)
{
    (void)room_id;
    (void)motion_detected;
    return ESP_OK;
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 25: pir_sensor.c */
/* ------------------------------------------------------------------------------ */


esp_err_t pir_sensor_init(void)
{
    ESP_LOGI(TAG, "PIR sensor proxy initialized (Entrance Hub)");
    return ESP_OK;
}

esp_err_t pir_sensor_simulate_motion(uint8_t room_id, bool active)
{
    if (room_id < 1 || room_id > 2) {
        return ESP_ERR_INVALID_ARG;
    }

    char topic[64];
    snprintf(topic, sizeof(topic), "%s/room/%u/control", CFG_MQTT_TOPIC_PREFIX, room_id);
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"cmd\":\"motion\",\"val\":%d}", active ? 1 : 0);

    ESP_LOGI(TAG, "Sent test motion simulation to Room %u (active=%d)", room_id, (int)active);
    return cocis_mqtt_publish(topic, payload);
}


/* ------------------------------------------------------------------------------ */
/* Subsystem 26: cli.c */
/* ------------------------------------------------------------------------------ */


static void print_prompt(void)
{
    printf("\n%s", auth_is_logged_in() ? "cocis(admin)# " : "cocis> ");
    fflush(stdout);
}

static bool require_admin(void)
{
    if (!auth_is_logged_in()) {
        printf("ERROR: Admin authentication required. Run: login <user> <pass>\n");
        logger_log_audit("ANONYMOUS", "UNAUTH_CMD_BLOCKED", "Attempted privileged CLI action");
        return false;
    }
    auth_touch_activity();
    return true;
}

static void cmd_help(void)
{
    printf("\n==============================================================================\n");
    printf("              CoCIS SMART CONTROL & MONITORING CLI REFERENCE\n");
    printf("==============================================================================\n");
    printf(" [MONITORING COMMANDS]\n");
    printf("  status                             System dashboard (Clock, Doors, Rooms, SD)\n");
    printf("  inside | occupancy                 List everyone currently inside CoCIS\n");
    printf("  rooms  | lights                    Lecture Room 1 & 2 PIR, lights & energy\n");
    printf("  log [n]                            Show last n (default 15) hash-chained logs\n");
    printf("  log user <uid>                     Filter access/audit logs for RFID <uid>\n");
    printf("  stats                              Access grant/deny & anti-passback metrics\n");
    printf("  nodes                              Distributed ESP32 terminal health & queues\n");
    printf("  daily_report                       Generate management daily summary report\n");
    printf("\n [ADMINISTRATION COMMANDS (Login Required: default admin / Cocis@2026)]\n");
    printf("  login <user> <pass> [2fa_uid]      Authenticate admin session\n");
    printf("  logout                             Close current admin session\n");
    printf("  passwd <old_pass> <new_pass>       Update salted SHA-256 admin password\n");
    printf("  schedule show                      Display weekly authorized access schedule\n");
    printf("  schedule set <day|all> <HH:MM> <HH:MM>  Set open/close hours (e.g. mon 07:00 21:00)\n");
    printf("  schedule close <day>               Close building for an entire day of week\n");
    printf("  override_exit <uid>                Grant 5-min supervisor safe-exit pass\n");
    printf("  emergency <on|off>                 Activate/clear fire evacuation unlock mode\n");
    printf("  user list                          List all enrolled RFID users and roles\n");
    printf("  user add <uid> <role> <name>       Enroll RFID tag (admin|staff|student|visitor)\n");
    printf("  user del <uid>                     Delete enrolled RFID tag\n");
    printf("  user enable|disable <uid>          Enable or suspend an enrolled RFID tag\n");
    printf("  sd status | verify | format        Inspect, verify SHA-256 chain, or format SD\n");
    printf("  time [set YYYY-MM-DD HH:MM:SS]     Show or configure East Africa Time (EAT)\n");
    printf("\n [SIMULATION & VERIFICATION HELPERS]\n");
    printf("  sim scan <entry|exit> <uid>        Simulate RFID card scan at Entry or Exit\n");
    printf("  sim motion <1|2> <on|off>          Simulate PIR motion in Lecture Room 1 or 2\n");
    printf("  sim node <1|2> <online|offline>    Simulate network link drop & store-forward\n");
    printf("  sim door_hold <entry|exit>         Simulate forced/propped door alarm\n");
    printf("  sim selftest                       Run automated 10-scenario coursework suite\n");
    printf("==============================================================================\n");
}

static void cmd_status(void)
{
    time_snapshot_t now;
    time_sync_get_now(&now);
    char iso[32];
    time_sync_format_iso(now.epoch, iso, sizeof(iso));

    weekly_schedule_t sched;
    schedule_get_weekly(&sched);
    const day_schedule_t *today = &sched.days[now.day_of_week];

    occupancy_stats_t ostats;
    occupancy_ledger_get_stats(&ostats);

    door_status_t d_in, d_out;
    door_get_status(DOOR_ID_ENTRY, &d_in);
    door_get_status(DOOR_ID_EXIT, &d_out);

    room_light_status_t r1, r2;
    light_get_room_status(1, &r1);
    light_get_room_status(2, &r2);

    sd_status_t sdst;
    storage_sd_get_status(&sdst);
    char tip[16];
    logger_get_chain_tip(tip, sizeof(tip));

    wifi_status_t wst;
    net_wifi_get_status(&wst);

    health_summary_t hst;
    health_get_summary(&hst);

    auth_status_t ast;
    auth_get_status(&ast);

    printf("\n==============================================================================\n");
    printf("             CoCIS SMART CONTROL & MONITORING SYSTEM - STATUS\n");
    printf("==============================================================================\n");
    printf(" System Clock (EAT) : %s (%s) [%s]\n",
           iso, schedule_day_name(now.day_of_week),
           now.time_valid ? (now.sntp_synced ? "VALID RTC+SNTP" : "VALID RTC") : "INVALID");
    if (today->enabled) {
        printf(" Building Schedule  : %s (Today's Window: %02u:%02u - %02u:%02u EAT)\n",
               schedule_is_open_now() ? "OPEN" : "CLOSED (After Hours)",
               today->open_hour, today->open_min, today->close_hour, today->close_min);
    } else {
        printf(" Building Schedule  : CLOSED ALL DAY (%s)\n", schedule_day_name(now.day_of_week));
    }
    printf(" Emergency Mode     : %s\n",
           schedule_is_emergency_mode() ? "!! ACTIVE - ALL DOORS UNLOCKED FOR EVACUATION !!" : "NORMAL OPERATION");
    printf(" Live Occupancy     : %lu person(s) inside (Peak: %lu | Entries: %lu | Exits: %lu)\n",
           (unsigned long)ostats.current_inside, (unsigned long)ostats.peak_inside,
           (unsigned long)ostats.total_entries, (unsigned long)ostats.total_exits);
    printf(" Main Entrance Doors: Entry=%s | Exit=%s\n",
           door_state_to_str(d_in.state), door_state_to_str(d_out.state));
    printf(" Lecture Room 1     : PIR=%-6s | Light=%-10s | Saved=%lu mWh\n",
           r1.pir_motion_active ? "MOTION" : "IDLE", light_fsm_to_str(r1.fsm_state),
           (unsigned long)r1.energy_saved_mwh);
    printf(" Lecture Room 2     : PIR=%-6s | Light=%-10s | Saved=%lu mWh\n",
           r2.pir_motion_active ? "MOTION" : "IDLE", light_fsm_to_str(r2.fsm_state),
           (unsigned long)r2.energy_saved_mwh);
    printf(" SD Card Storage    : %s | Chain Tip=%s | Writes=%lu\n",
           sdst.mounted ? "MOUNTED (/sdcard)" : "RAM FALLBACK", tip, (unsigned long)sdst.write_ok_count);
    printf(" Distributed Nodes  : %u/3 Nodes ONLINE | Wi-Fi=%s (%s) | MQTT=%s\n",
           (unsigned)hst.online_nodes,
           wst.connected ? "CONNECTED" : "DISCONNECTED", wst.ip_addr,
           net_protocol_is_mqtt_connected() ? "ONLINE" : "LOCAL_BUS");
    printf(" Admin CLI Session  : %s (Free Heap: %lu B, Uptime: %lu s)\n",
           ast.logged_in ? "AUTHENTICATED" : "LOCKED (Guest)",
           (unsigned long)hst.free_heap_bytes, (unsigned long)hst.uptime_sec);
    printf("==============================================================================\n");
}

static void cmd_inside(void)
{
    occupant_record_t list[CFG_USER_DB_MAX_USERS];
    size_t count = occupancy_ledger_list_inside(list, CFG_USER_DB_MAX_USERS);
    time_snapshot_t now;
    time_sync_get_now(&now);

    printf("\n+------+------------+--------------------------+---------+---------------------+----------+\n");
    printf("| Slot | Tag UID    | Full Name                | Role    | Entry Time (EAT)    | Duration |\n");
    printf("+------+------------+--------------------------+---------+---------------------+----------+\n");
    for (size_t i = 0; i < count; i++) {
        char entry_iso[32];
        time_sync_format_iso(list[i].entry_epoch, entry_iso, sizeof(entry_iso));
        int64_t dur = (now.epoch >= list[i].entry_epoch) ? (now.epoch - list[i].entry_epoch) : 0;
        int hh = (int)(dur / 3600);
        int mm = (int)((dur % 3600) / 60);
        int ss = (int)(dur % 60);
        printf("| %02u   | %-10s | %-24.24s | %-7s | %-19s | %02d:%02d:%02d |\n",
               (unsigned)(i + 1),
               list[i].uid,
               list[i].name,
               user_db_role_to_str(list[i].role),
               entry_iso,
               hh, mm, ss);
    }
    if (count == 0) {
        printf("| --   | (Building is currently unoccupied - 0 persons inside CoCIS)                  |\n");
    }
    printf("+------+------------+--------------------------+---------+---------------------+----------+\n");
    printf("Total Currently Inside CoCIS: %u person(s)\n", (unsigned)count);
}

static void cmd_rooms(void)
{
    printf("\n+--------+----------+------------+------------+-----------+------------+-------------+--------------+\n");
    printf("| Room   | Node St. | PIR Sensor | Light FSM  | Relay Pin | Time Left  | Light On(s) | Energy Saved |\n");
    printf("+--------+----------+------------+------------+-----------+------------+-------------+--------------+\n");

    uint32_t total_saved = 0;
    for (uint8_t r = 1; r <= CFG_ROOM_COUNT; r++) {
        room_light_status_t st;
        light_get_room_status(r, &st);
        node_net_status_t nst;
        net_protocol_get_node_status((r == 1) ? NODE_ID_ROOM_1 : NODE_ID_ROOM_2, &nst);
        total_saved += st.energy_saved_mwh;

        printf("| Room %u | %-8s | %-10s | %-10s | %-9s | %8lu s | %9lu s | %8lu mWh |\n",
               (unsigned)r,
               nst.link_simulated_online ? "ONLINE" : "OFFLINE",
               st.pir_motion_active ? "MOTION" : "VACANT",
               light_fsm_to_str(st.fsm_state),
               st.relay_on ? "HIGH (ON)" : "LOW (OFF)",
               (unsigned long)st.seconds_until_off,
               (unsigned long)st.total_light_on_sec,
               (unsigned long)st.energy_saved_mwh);
    }
    printf("+--------+----------+------------+------------+-----------+------------+-------------+--------------+\n");
    uint32_t ugx_equiv = (total_saved * CFG_UGX_PER_KWH) / 1000U;
    printf("Cumulative Building Energy Saved: %lu mWh (Scaled Pilot Cost Reductions: ~%lu UGX)\n",
           (unsigned long)total_saved, (unsigned long)ugx_equiv);
}

static void cmd_log(int argc, char **argv)
{
    size_t limit = 15;
    const char *filter_uid = NULL;

    if (argc >= 2) {
        if (strcasecmp(argv[1], "user") == 0 && argc >= 3) {
            filter_uid = argv[2];
            limit = 32;
        } else {
            int val = atoi(argv[1]);
            if (val > 0 && val <= CFG_LOG_RAM_RING_CAP) {
                limit = (size_t)val;
            }
        }
    }

    logger_flush_sync(100);
    log_entry_t entries[CFG_LOG_RAM_RING_CAP];
    size_t count = logger_get_recent(entries, limit, filter_uid);

    printf("\n+------+---------------------+------------+----------------------+--------------------------+----------+----------+\n");
    printf("| Seq  | Timestamp (EAT)     | UID/Actor  | Action               | Detail                   | Val/Cnt  | SHA256   |\n");
    printf("+------+---------------------+------------+----------------------+--------------------------+----------+----------+\n");
    for (size_t i = 0; i < count; i++) {
        char ts[32];
        time_sync_format_iso(entries[i].timestamp, ts, sizeof(ts));
        printf("| %04lu | %-19s | %-10.10s | %-20.20s | %-24.24s | %8ld | %-8s |\n",
               (unsigned long)entries[i].seq_id,
               ts,
               entries[i].tag_or_actor,
               entries[i].action,
               entries[i].detail,
               (long)entries[i].value,
               entries[i].chain_hash);
    }
    if (count == 0) {
        printf("| ---- | No matching log records found                                                                        |\n");
    }
    printf("+------+---------------------+------------+----------------------+--------------------------+----------+----------+\n");
}

static void cmd_stats(void)
{
    access_stats_t astats;
    access_control_get_stats(&astats);
    occupancy_stats_t ostats;
    occupancy_ledger_get_stats(&ostats);

    printf("\n+----------------------------------------+------------+\n");
    printf("| Security & Performance Metric          |      Value |\n");
    printf("+----------------------------------------+------------+\n");
    printf("| Granted Entries                        | %10lu |\n", (unsigned long)astats.granted_entry);
    printf("| Granted Exits                          | %10lu |\n", (unsigned long)astats.granted_exit);
    printf("| Denied: Unknown RFID Tag               | %10lu |\n", (unsigned long)astats.denied_unknown);
    printf("| Denied: Inactive / Suspended User      | %10lu |\n", (unsigned long)astats.denied_inactive);
    printf("| Denied: Expired Visitor Badge          | %10lu |\n", (unsigned long)astats.denied_expired);
    printf("| Denied: Outside Schedule (Entry)       | %10lu |\n", (unsigned long)astats.denied_schedule_entry);
    printf("| Denied: Outside Schedule (Exit)        | %10lu |\n", (unsigned long)astats.denied_schedule_exit);
    printf("| Denied: Anti-Passback Violations       | %10lu |\n", (unsigned long)astats.denied_antipassback);
    printf("| Current Building Occupancy             | %10lu |\n", (unsigned long)ostats.current_inside);
    printf("| Peak Building Occupancy                | %10lu |\n", (unsigned long)ostats.peak_inside);
    printf("| Boot-Recovered Occupants from SD       | %10lu |\n", (unsigned long)ostats.recovered_on_boot);
    printf("| Total Energy Saved (mWh)               | %10lu |\n", (unsigned long)light_get_total_energy_saved_mwh());
    printf("+----------------------------------------+------------+\n");

    printf("\n[Hierarchical Mutex Lock Diagnostics]\n");
    printf("+----+---------------+--------------+--------------+\n");
    printf("| Lv | Lock Name     | Acquires     | Timeouts     |\n");
    printf("+----+---------------+--------------+--------------+\n");
    for (int lv = LOCK_LEVEL_CONFIG; lv < LOCK_LEVEL_COUNT; lv++) {
        sync_lock_stats_t lst;
        if (sync_get_stats((sync_lock_level_t)lv, &lst) == ESP_OK) {
            printf("| %2d | %-13s | %12lu | %12lu |\n",
                   lv, lst.name, (unsigned long)lst.acquire_count, (unsigned long)lst.timeout_count);
        }
    }
    printf("+----+---------------+--------------+--------------+\n");
}

static void cmd_nodes(void)
{
    printf("\n+----+-----------------------+---------+--------+--------+--------+--------+----------+----------+\n");
    printf("| ID | Terminal Name         | Link    | TxSeq  | RxSeq  | ACKed  | DupSup | Buffered | Replayed |\n");
    printf("+----+-----------------------+---------+--------+--------+--------+--------+----------+----------+\n");
    for (int i = 0; i < NODE_ID_COUNT; i++) {
        node_net_status_t st;
        if (net_protocol_get_node_status((node_id_t)i, &st) == ESP_OK) {
            printf("| %2d | %-21.21s | %-7s | %6lu | %6lu | %6lu | %6lu | %8lu | %8lu |\n",
                   i,
                   st.name,
                   st.link_simulated_online ? "ONLINE" : "OFFLINE",
                   (unsigned long)st.last_tx_seq,
                   (unsigned long)st.last_rx_seq,
                   (unsigned long)st.acked_count,
                   (unsigned long)st.dup_suppressed_count,
                   (unsigned long)st.buffered_offline_count,
                   (unsigned long)st.replayed_count);
        }
    }
    printf("+----+-----------------------+---------+--------+--------+--------+--------+----------+----------+\n");
}

static void cmd_daily_report(void)
{
    printf("\n******************************************************************************\n");
    printf("*        MAKERERE UNIVERSITY - CoCIS DAILY EXECUTIVE SECURITY REPORT         *\n");
    printf("******************************************************************************\n");
    cmd_status();
    cmd_inside();
    cmd_rooms();
    cmd_stats();
}

static void cmd_schedule(int argc, char **argv)
{
    if (argc < 2 || strcasecmp(argv[1], "show") == 0) {
        weekly_schedule_t sched;
        schedule_get_weekly(&sched);
        printf("\n+-----+-----------+---------------+---------------+------------------+\n");
        printf("| Day | Status    | Open Time EAT | Close Time    | Policy           |\n");
        printf("+-----+-----------+---------------+---------------+------------------+\n");
        for (int d = 0; d < 7; d++) {
            const day_schedule_t *ds = &sched.days[d];
            printf("| %-3s | %-9s |     %02u:%02u     |     %02u:%02u     | %-16s |\n",
                   schedule_day_name(d),
                   ds->enabled ? "ENABLED" : "CLOSED",
                   ds->open_hour, ds->open_min,
                   ds->close_hour, ds->close_min,
                   ds->enabled ? "Entry/Exit Enfc." : "Admin/Evac Only");
        }
        printf("+-----+-----------+---------------+---------------+------------------+\n");
        return;
    }

    if (!require_admin()) {
        return;
    }

    if (strcasecmp(argv[1], "close") == 0 && argc >= 3) {
        int d = schedule_parse_day_name(argv[2]);
        if (d < 0) {
            printf("ERROR: Invalid day '%s'. Use sun, mon, tue, wed, thu, fri, sat.\n", argv[2]);
            return;
        }
        schedule_set_day(d, false, 0, 0, 0, 0);
        net_protocol_broadcast_schedule_sync();
        logger_log_audit("admin", "SCHEDULE_CLOSE", schedule_day_name(d));
        printf("OK: Marked %s as CLOSED all day.\n", schedule_day_name(d));
        return;
    }

    if (strcasecmp(argv[1], "set") == 0 && argc >= 5) {
        int oh = 0, om = 0, ch = 0, cm = 0;
        if (sscanf(argv[3], "%d:%d", &oh, &om) != 2 || sscanf(argv[4], "%d:%d", &ch, &cm) != 2) {
            printf("ERROR: Time format must be HH:MM (e.g. schedule set mon 07:00 21:00)\n");
            return;
        }

        if (strcasecmp(argv[2], "all") == 0) {
            for (int d = 0; d < 7; d++) {
                if (schedule_set_day(d, true, (uint8_t)oh, (uint8_t)om, (uint8_t)ch, (uint8_t)cm) != ESP_OK) {
                    printf("ERROR: Invalid time window %s - %s\n", argv[3], argv[4]);
                    return;
                }
            }
            net_protocol_broadcast_schedule_sync();
            logger_log_audit("admin", "SCHEDULE_SET_ALL", argv[3]);
            printf("OK: Updated ALL days to %02d:%02d - %02d:%02d EAT.\n", oh, om, ch, cm);
            return;
        }

        int d = schedule_parse_day_name(argv[2]);
        if (d < 0) {
            printf("ERROR: Unknown day '%s'. Use sun..sat or all.\n", argv[2]);
            return;
        }
        if (schedule_set_day(d, true, (uint8_t)oh, (uint8_t)om, (uint8_t)ch, (uint8_t)cm) != ESP_OK) {
            printf("ERROR: Close time must be strictly after open time.\n");
            return;
        }
        net_protocol_broadcast_schedule_sync();
        char detail[48];
        snprintf(detail, sizeof(detail), "%s %02d:%02d-%02d:%02d", schedule_day_name(d), oh, om, ch, cm);
        logger_log_audit("admin", "SCHEDULE_SET", detail);
        printf("OK: Updated %s schedule to %02d:%02d - %02d:%02d EAT.\n", schedule_day_name(d), oh, om, ch, cm);
        return;
    }

    printf("Usage: schedule show | schedule set <day|all> <HH:MM> <HH:MM> | schedule close <day>\n");
}

static void cmd_user(int argc, char **argv)
{
    if (argc < 2 || strcasecmp(argv[1], "list") == 0) {
        user_record_t users[CFG_USER_DB_MAX_USERS];
        size_t n = user_db_list(users, CFG_USER_DB_MAX_USERS);
        printf("\n+------------+--------------------------+---------+--------+----------+\n");
        printf("| Tag UID    | Full Name                | Role    | Active | Location |\n");
        printf("+------------+--------------------------+---------+--------+----------+\n");
        for (size_t i = 0; i < n; i++) {
            bool inside = occupancy_ledger_is_inside(users[i].uid);
            printf("| %-10s | %-24.24s | %-7s | %-6s | %-8s |\n",
                   users[i].uid,
                   users[i].name,
                   user_db_role_to_str(users[i].role),
                   users[i].active ? "YES" : "NO",
                   inside ? "INSIDE" : "OUTSIDE");
        }
        printf("+------------+--------------------------+---------+--------+----------+\n");
        printf("Total Enrolled Users: %u / %d\n", (unsigned)n, CFG_USER_DB_MAX_USERS);
        return;
    }

    if (!require_admin()) {
        return;
    }

    if (strcasecmp(argv[1], "add") == 0 && argc >= 5) {
        user_role_t role;
        if (!user_db_parse_role(argv[3], &role)) {
            printf("ERROR: Invalid role '%s'. Use admin, staff, student, or visitor.\n", argv[3]);
            return;
        }
        char full_name[CFG_USER_NAME_MAX_LEN] = { 0 };
        for (int i = 4; i < argc; i++) {
            if (i > 4) {
                strncat(full_name, " ", sizeof(full_name) - strlen(full_name) - 1);
            }
            strncat(full_name, argv[i], sizeof(full_name) - strlen(full_name) - 1);
        }
        int64_t expiry = 0;
        if (role == USER_ROLE_VISITOR) {
            time_snapshot_t now;
            time_sync_get_now(&now);
            expiry = now.epoch + CFG_VISITOR_DEFAULT_TTL_SEC;
        }
        if (user_db_add(argv[2], full_name, role, true, expiry) == ESP_OK) {
            logger_log_audit("admin", "USER_ADD", argv[2]);
            printf("OK: Enrolled %s (%s) with role %s.\n", argv[2], full_name, user_db_role_to_str(role));
        } else {
            printf("ERROR: Failed to enroll user (database full or invalid UID).\n");
        }
        return;
    }

    if (strcasecmp(argv[1], "del") == 0 && argc >= 3) {
        if (user_db_remove(argv[2]) == ESP_OK) {
            logger_log_audit("admin", "USER_DELETE", argv[2]);
            printf("OK: Removed user %s.\n", argv[2]);
        } else {
            printf("ERROR: UID %s not found.\n", argv[2]);
        }
        return;
    }

    if ((strcasecmp(argv[1], "enable") == 0 || strcasecmp(argv[1], "disable") == 0) && argc >= 3) {
        bool en = (strcasecmp(argv[1], "enable") == 0);
        if (user_db_set_active(argv[2], en) == ESP_OK) {
            logger_log_audit("admin", en ? "USER_ENABLE" : "USER_DISABLE", argv[2]);
            printf("OK: User %s is now %s.\n", argv[2], en ? "ACTIVE" : "DISABLED");
        } else {
            printf("ERROR: UID %s not found.\n", argv[2]);
        }
        return;
    }

    printf("Usage: user list | user add <uid> <role> <name> | user del <uid> | user enable|disable <uid>\n");
}

static void cmd_sd(int argc, char **argv)
{
    if (argc < 2 || strcasecmp(argv[1], "status") == 0) {
        sd_status_t st;
        storage_sd_get_status(&st);
        char tip[16];
        logger_get_chain_tip(tip, sizeof(tip));
        printf("\n[SD Card & Hash-Chain Storage Status]\n");
        printf("  Mounted        : %s\n", st.mounted ? "YES (/sdcard via SPI2 SDSPI)" : "NO (RAM Ring-Buffer Active)");
        printf("  access.csv     : %lu bytes\n", (unsigned long)st.access_log_bytes);
        printf("  lighting.csv   : %lu bytes\n", (unsigned long)st.lighting_log_bytes);
        printf("  audit.csv      : %lu bytes\n", (unsigned long)st.audit_log_bytes);
        printf("  Write Count    : %lu OK / %lu Errors\n",
               (unsigned long)st.write_ok_count, (unsigned long)st.write_err_count);
        printf("  SHA-256 Tip    : %s\n", tip);
        return;
    }

    if (strcasecmp(argv[1], "verify") == 0) {
        uint32_t checked = 0;
        bool valid = false;
        logger_verify_integrity(&checked, &valid);
        char tip[16];
        logger_get_chain_tip(tip, sizeof(tip));
        printf("SHA-256 Log Chain Verification: Checked %lu record(s) -> %s (Tip=%s)\n",
               (unsigned long)checked, valid ? "INTEGRITY OK (UNTAMPERED)" : "TAMPER DETECTED!", tip);
        return;
    }

    if (strcasecmp(argv[1], "format") == 0) {
        if (!require_admin()) {
            return;
        }
        logger_reset_chain();
        logger_log_audit("admin", "SD_FORMAT", "Cleared logs and reset genesis hash chain");
        printf("OK: SD CSV logs formatted and SHA-256 chain reset to genesis.\n");
        return;
    }

    printf("Usage: sd status | sd verify | sd format\n");
}

static void cmd_time(int argc, char **argv)
{
    if (argc >= 4 && strcasecmp(argv[1], "set") == 0) {
        if (!require_admin()) {
            return;
        }
        int yr = 0, mo = 0, dy = 0, hh = 0, mm = 0, ss = 0;
        if (sscanf(argv[2], "%d-%d-%d", &yr, &mo, &dy) != 3 ||
            sscanf(argv[3], "%d:%d:%d", &hh, &mm, &ss) != 3) {
            printf("Usage: time set YYYY-MM-DD HH:MM:SS\n");
            return;
        }
        if (time_sync_set_datetime(yr, mo, dy, hh, mm, ss) == ESP_OK) {
            logger_log_audit("admin", "TIME_SET", argv[2]);
            printf("OK: System clock updated to %04d-%02d-%02d %02d:%02d:%02d EAT.\n",
                   yr, mo, dy, hh, mm, ss);
        } else {
            printf("ERROR: Invalid date/time parameters.\n");
        }
        return;
    }

    time_snapshot_t now;
    time_sync_get_now(&now);
    char iso[32];
    time_sync_format_iso(now.epoch, iso, sizeof(iso));
    printf("Current System Clock: %s EAT (%s, Epoch=%lld, Valid=%s)\n",
           iso, schedule_day_name(now.day_of_week), (long long)now.epoch,
           now.time_valid ? "YES" : "NO");
}

static void run_selftest_suite(void)
{
    printf("\n==============================================================================\n");
    printf("       RUNNING CoCIS AUTOMATED 10-SCENARIO COURSEWORK VERIFICATION SUITE\n");
    printf("==============================================================================\n");

    int passed = 0;
    int total = 10;

    /* Reset baseline state for deterministic self-test */
    schedule_set_emergency_mode(false);
    schedule_reset_defaults();
    occupancy_ledger_clear_all();
    auth_reset_lockout();
    auth_logout();
    time_sync_set_datetime(2026, 10, 24, 9, 0, 0); /* Saturday 09:00:00 EAT (Open: 08:00-17:00) */

    /* Test 1: Valid Entry */
    access_result_t r1 = access_control_evaluate_sync(RFID_READER_ENTRY, "04A1B2C3");
    bool t1 = (r1 == ACCESS_RESULT_GRANTED_NORMAL) && (occupancy_ledger_get_count() == 1);
    printf(" [Test 01] Valid RFID Entry & Occupancy Increment      : %s\n", t1 ? "PASS" : "FAIL");
    if (t1) passed++;

    /* Test 2: Anti-Passback on Duplicate Entry */
    access_result_t r2 = access_control_evaluate_sync(RFID_READER_ENTRY, "04A1B2C3");
    bool t2 = (r2 == ACCESS_RESULT_DENIED_ANTIPASSBACK_ENTRY) && (occupancy_ledger_get_count() == 1);
    printf(" [Test 02] Anti-Passback Violation (Duplicate Entry)   : %s\n", t2 ? "PASS" : "FAIL");
    if (t2) passed++;

    /* Test 3: Valid Exit & Occupancy Decrement */
    access_result_t r3 = access_control_evaluate_sync(RFID_READER_EXIT, "04A1B2C3");
    bool t3 = (r3 == ACCESS_RESULT_GRANTED_NORMAL) && (occupancy_ledger_get_count() == 0);
    printf(" [Test 03] Valid RFID Exit & Occupancy Decrement       : %s\n", t3 ? "PASS" : "FAIL");
    if (t3) passed++;

    /* Test 4: Anti-Passback on Exit When Outside */
    access_result_t r4 = access_control_evaluate_sync(RFID_READER_EXIT, "04A1B2C3");
    bool t4 = (r4 == ACCESS_RESULT_DENIED_ANTIPASSBACK_EXIT);
    printf(" [Test 04] Anti-Passback Violation (Exit When Outside) : %s\n", t4 ? "PASS" : "FAIL");
    if (t4) passed++;

    /* Test 5: Unknown Tag & Inactive User Denial */
    access_result_t r5a = access_control_evaluate_sync(RFID_READER_ENTRY, "DEADBEEF");
    access_result_t r5b = access_control_evaluate_sync(RFID_READER_ENTRY, "0499DEAD");
    bool t5 = (r5a == ACCESS_RESULT_DENIED_UNKNOWN_TAG) && (r5b == ACCESS_RESULT_DENIED_USER_INACTIVE);
    printf(" [Test 05] Unknown Tag & Inactive User Rejection       : %s\n", t5 ? "PASS" : "FAIL");
    if (t5) passed++;

    /* Test 6: Schedule Enforcement (After-Hours Entry & Exit Denial + Supervisor Override) */
    access_control_evaluate_sync(RFID_READER_ENTRY, "04C3D4E5"); /* Sarah enters at 09:00 */
    time_sync_set_datetime(2026, 10, 24, 22, 0, 0); /* 22:00 EAT (After 17:00 Sat close) */
    access_result_t r6_in = access_control_evaluate_sync(RFID_READER_ENTRY, "04D4E5F6");
    access_result_t r6_out_deny = access_control_evaluate_sync(RFID_READER_EXIT, "04C3D4E5");
    schedule_grant_exit_override("04C3D4E5");
    access_result_t r6_out_ovr = access_control_evaluate_sync(RFID_READER_EXIT, "04C3D4E5");
    bool t6 = (r6_in == ACCESS_RESULT_DENIED_OUTSIDE_SCHED_ENTRY) &&
              (r6_out_deny == ACCESS_RESULT_DENIED_OUTSIDE_SCHED_EXIT) &&
              (r6_out_ovr == ACCESS_RESULT_GRANTED_SUPERVISOR_EXIT) &&
              (occupancy_ledger_get_count() == 0);
    printf(" [Test 06] Schedule Entry/Exit Block & Supv. Override  : %s\n", t6 ? "PASS" : "FAIL");
    if (t6) passed++;

    /* Test 7: Emergency Evacuation Mode After-Hours Safe Egress */
    time_sync_set_datetime(2026, 10, 24, 10, 0, 0);
    access_control_evaluate_sync(RFID_READER_ENTRY, "04D4E5F6"); /* David enters */
    time_sync_set_datetime(2026, 10, 24, 23, 0, 0); /* After hours */
    schedule_set_emergency_mode(true);
    door_set_emergency_all(true);
    access_result_t r7_evac = access_control_evaluate_sync(RFID_READER_EXIT, "04D4E5F6");
    schedule_set_emergency_mode(false);
    door_set_emergency_all(false);
    bool t7 = (r7_evac == ACCESS_RESULT_GRANTED_EMERGENCY_EVAC) && (occupancy_ledger_get_count() == 0);
    printf(" [Test 07] Emergency Evacuation Mode Safe Egress       : %s\n", t7 ? "PASS" : "FAIL");
    if (t7) passed++;

    /* Test 8: Lecture Room PIR Occupancy -> Light ON -> Grace -> Vacancy Timeout OFF */
    pir_sensor_simulate_motion(1, true);
    room_light_status_t l_on;
    light_get_room_status(1, &l_on);
    pir_sensor_simulate_motion(1, false);
    for (int i = 0; i < 22; i++) {
        light_tick_1hz();
    }
    room_light_status_t l_off;
    light_get_room_status(1, &l_off);
    bool t8 = l_on.relay_on && (l_on.fsm_state == LIGHT_FSM_ON_ACTIVE) &&
              (!l_off.relay_on) && (l_off.fsm_state == LIGHT_FSM_OFF_VACANT) &&
              (l_off.energy_saved_mwh > 0);
    printf(" [Test 08] Room PIR -> Light ON -> Grace -> Timeout OFF: %s\n", t8 ? "PASS" : "FAIL");
    if (t8) passed++;

    /* Test 9: Node Offline Store-and-Forward & Idempotent Replay on Reconnect */
    net_protocol_set_node_online(NODE_ID_ROOM_2, false);
    pir_sensor_simulate_motion(2, true);
    pir_sensor_simulate_motion(2, false);
    node_net_status_t n2_off;
    net_protocol_get_node_status(NODE_ID_ROOM_2, &n2_off);
    net_protocol_set_node_online(NODE_ID_ROOM_2, true);
    node_net_status_t n2_on;
    net_protocol_get_node_status(NODE_ID_ROOM_2, &n2_on);
    bool t9 = (n2_off.buffered_offline_count >= 2) &&
              (n2_on.buffered_offline_count == 0) &&
              (n2_on.replayed_count >= 2) &&
              (n2_on.dup_suppressed_count >= 1);
    printf(" [Test 09] Offline Store-and-Forward & Dedup Replay    : %s\n", t9 ? "PASS" : "FAIL");
    if (t9) passed++;

    /* Test 10: Salted SHA-256 Admin Auth Lockout & Log Hash-Chain Integrity */
    auth_login("admin", "bad1", NULL);
    auth_login("admin", "bad2", NULL);
    auth_login("admin", "bad3", NULL);
    esp_err_t lock_err = auth_login("admin", CFG_AUTH_DEFAULT_PASS, NULL);
    auth_reset_lockout();
    esp_err_t ok_err = auth_login("admin", CFG_AUTH_DEFAULT_PASS, NULL);
    uint32_t checked = 0;
    bool chain_ok = false;
    logger_verify_integrity(&checked, &chain_ok);
    bool t10 = (lock_err == ESP_ERR_INVALID_STATE) && (ok_err == ESP_OK) && chain_ok && (checked > 0);
    printf(" [Test 10] Salted SHA-256 Auth Lockout & Hash Chain    : %s\n", t10 ? "PASS" : "FAIL");
    if (t10) passed++;

    /* Restore clock to Saturday 08:35:00 EAT */
    time_sync_set_datetime(2026, 10, 24, 8, 35, 0);

    printf("------------------------------------------------------------------------------\n");
    printf(" SELF-TEST SUMMARY: %d / %d SCENARIOS PASSED (%s)\n",
           passed, total, (passed == total) ? "ALL REQUIREMENTS VERIFIED" : "CHECK LOGS");
    printf("==============================================================================\n");
}

static void cmd_sim(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sim scan <entry|exit> <uid> | sim motion <1|2> <on|off> | sim node <1|2> <online|offline> | sim door_hold <entry|exit> | sim selftest\n");
        return;
    }

    if (strcasecmp(argv[1], "selftest") == 0) {
        run_selftest_suite();
        return;
    }

    if (strcasecmp(argv[1], "scan") == 0 && argc >= 4) {
        rfid_reader_t rdr = (strcasecmp(argv[2], "exit") == 0) ? RFID_READER_EXIT : RFID_READER_ENTRY;
        rfid_inject_scan(rdr, argv[3]);
        vTaskDelay(pdMS_TO_TICKS(80));
        return;
    }

    if (strcasecmp(argv[1], "motion") == 0 && argc >= 4) {
        uint8_t room = (uint8_t)atoi(argv[2]);
        bool on = (strcasecmp(argv[3], "on") == 0 || strcmp(argv[3], "1") == 0);
        if (pir_sensor_simulate_motion(room, on) == ESP_OK) {
            printf("OK: Simulated PIR motion %s in Lecture Room %u.\n", on ? "ACTIVE" : "IDLE", (unsigned)room);
        } else {
            printf("ERROR: Room must be 1 or 2.\n");
        }
        return;
    }

    if (strcasecmp(argv[1], "node") == 0 && argc >= 4) {
        int r = atoi(argv[2]);
        if (r != 1 && r != 2) {
            printf("ERROR: Node must be 1 (Room 1) or 2 (Room 2).\n");
            return;
        }
        bool online = (strcasecmp(argv[3], "online") == 0 || strcasecmp(argv[3], "on") == 0);
        net_protocol_set_node_online((node_id_t)r, online);
        printf("OK: Node %d (Lecture Room %d) link state set to %s.\n",
               r, r, online ? "ONLINE" : "OFFLINE (Store-and-Forward Active)");
        return;
    }

    if (strcasecmp(argv[1], "door_hold") == 0 && argc >= 3) {
        door_id_t d = (strcasecmp(argv[2], "exit") == 0) ? DOOR_ID_EXIT : DOOR_ID_ENTRY;
        door_simulate_forced_hold(d);
        printf("OK: Triggered forced/held-open tailgate alarm on %s door.\n",
               (d == DOOR_ID_ENTRY) ? "ENTRY" : "EXIT");
        return;
    }

    printf("Unknown sim command. Type 'help' for syntax.\n");
}

esp_err_t cli_execute_line(char *line)
{
    if (line == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *argv[12] = { 0 };
    int argc = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(line, " \t\r\n", &saveptr);
    while (tok != NULL && argc < 12) {
        argv[argc++] = tok;
        tok = strtok_r(NULL, " \t\r\n", &saveptr);
    }

    if (argc == 0) {
        return ESP_OK;
    }

    const char *cmd = argv[0];
    if (strcasecmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        cmd_help();
    } else if (strcasecmp(cmd, "status") == 0) {
        cmd_status();
    } else if (strcasecmp(cmd, "inside") == 0 || strcasecmp(cmd, "occupancy") == 0) {
        cmd_inside();
    } else if (strcasecmp(cmd, "rooms") == 0 || strcasecmp(cmd, "lights") == 0) {
        cmd_rooms();
    } else if (strcasecmp(cmd, "log") == 0) {
        cmd_log(argc, argv);
    } else if (strcasecmp(cmd, "stats") == 0) {
        cmd_stats();
    } else if (strcasecmp(cmd, "nodes") == 0) {
        cmd_nodes();
    } else if (strcasecmp(cmd, "daily_report") == 0) {
        cmd_daily_report();
    } else if (strcasecmp(cmd, "login") == 0) {
        if (argc < 3) {
            printf("Usage: login <username> <password> [admin_rfid_uid]\n");
        } else {
            const char *uid = (argc >= 4) ? argv[3] : NULL;
            esp_err_t err = auth_login(argv[1], argv[2], uid);
            if (err == ESP_OK) {
                printf("OK: Authenticated as '%s'. Admin commands unlocked.\n", argv[1]);
            } else if (err == ESP_ERR_INVALID_STATE) {
                auth_status_t st;
                auth_get_status(&st);
                printf("LOCKED OUT: Too many failed attempts. Wait %lu seconds.\n",
                       (unsigned long)st.lockout_remaining_sec);
            } else {
                printf("DENIED: Invalid admin credentials.\n");
            }
        }
    } else if (strcasecmp(cmd, "logout") == 0) {
        auth_logout();
        printf("OK: Admin session logged out.\n");
    } else if (strcasecmp(cmd, "passwd") == 0) {
        if (!require_admin()) {
            return ESP_OK;
        }
        if (argc < 3) {
            printf("Usage: passwd <old_password> <new_password>\n");
        } else if (auth_change_password(argv[1], argv[2]) == ESP_OK) {
            printf("OK: Admin password re-salted and updated in NVS.\n");
        } else {
            printf("ERROR: Password change failed (check old password and min length 6).\n");
        }
    } else if (strcasecmp(cmd, "schedule") == 0) {
        cmd_schedule(argc, argv);
    } else if (strcasecmp(cmd, "override_exit") == 0) {
        if (!require_admin()) {
            return ESP_OK;
        }
        if (argc < 2) {
            printf("Usage: override_exit <uid>\n");
        } else {
            schedule_grant_exit_override(argv[1]);
            logger_log_audit("admin", "SUPERVISOR_EXIT_PASS", argv[1]);
            printf("OK: Granted 5-minute supervisor safe-exit override for UID %s.\n", argv[1]);
        }
    } else if (strcasecmp(cmd, "emergency") == 0) {
        if (!require_admin()) {
            return ESP_OK;
        }
        if (argc < 2) {
            printf("Usage: emergency <on|off>\n");
        } else {
            bool on = (strcasecmp(argv[1], "on") == 0);
            schedule_set_emergency_mode(on);
            door_set_emergency_all(on);
            net_protocol_broadcast_emergency(on);
            logger_log_audit("admin", on ? "EMERGENCY_ON" : "EMERGENCY_OFF", "CLI command");
            printf("!! EMERGENCY EVACUATION MODE %s !!\n", on ? "ACTIVATED (ALL DOORS UNLOCKED)" : "CLEARED");
            if (on) {
                printf("Roll-Call of Occupants Currently Inside Building:\n");
                cmd_inside();
            }
        }
    } else if (strcasecmp(cmd, "user") == 0) {
        cmd_user(argc, argv);
    } else if (strcasecmp(cmd, "sd") == 0) {
        cmd_sd(argc, argv);
    } else if (strcasecmp(cmd, "time") == 0) {
        cmd_time(argc, argv);
    } else if (strcasecmp(cmd, "sim") == 0) {
        cmd_sim(argc, argv);
    } else {
        printf("Unknown command '%s'. Type 'help' for available commands.\n", cmd);
    }

    return ESP_OK;
}

static void cli_task(void *arg)
{
    (void)arg;
    char line_buf[160];
    size_t pos = 0;

    printf("\n==============================================================================\n");
    printf("  %s (%s)\n", CFG_SYSTEM_NAME, CFG_FIRMWARE_VERSION);
    printf("  College of Computing and Information Sciences (CoCIS), Makerere University\n");
    printf("  Type 'help' for commands, 'status' for overview, or 'sim selftest' to test.\n");
    printf("==============================================================================\n");
    print_prompt();

    while (1) {
        uint8_t ch = 0;
        int len = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            printf("\n");
            line_buf[pos] = '\0';
            if (pos > 0) {
                cli_execute_line(line_buf);
                pos = 0;
            }
            print_prompt();
        } else if (ch == 0x08 || ch == 0x7F) {
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
        } else if (isprint(ch) && pos + 1 < sizeof(line_buf)) {
            line_buf[pos++] = (char)ch;
            putchar((int)ch);
            fflush(stdout);
        }
    }
}

esp_err_t cli_init(void)
{
    uart_config_t ucfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };
    uart_driver_install(UART_NUM_0, 512, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_0, &ucfg);

    if (xTaskCreate(cli_task, "cli_task", 8192, NULL, 4, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Interactive UART0 management CLI initialized");
    return ESP_OK;
}


/* ============================================================================== */
/* 4. MASTER APPLICATION ENTRY POINT (setup & loop for Wokwi Web)                */
/* ============================================================================== */

void setup(void)
{
    printf("\n");
    printf("====================================================================\n");
    printf("    CoCIS SMART CONTROL SYSTEM - MAIN ENTRANCE COORDINATOR (HUB)    \n");
    printf("    Makerere University | BSE III & CS II Coursework Project 2026   \n");
    printf("    Firmware: v2.0.0-HUB | Target: ESP32 | Simulator: Wokwi Web     \n");
    printf("====================================================================\n\n");

    /* 1. Core Synchronization & Cryptographic Systems */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    sync_init();
    cocis_sync_init();
    cocis_crypto_init();
    time_sync_init();
    cocis_time_init();
    cocis_health_init(NODE_ID_ENTRANCE);
    config_init();

    /* 2. Board Peripherals */
    board_pins_init();
    display_init();
    buzzer_init();
    door_init();

    /* 3. Authoritative Storage & Business Logic */
    storage_sd_init();
    user_db_init();
    schedule_init();
    occupancy_ledger_init();
    auth_init();
    logger_init();
    access_control_init();
    rfid_init(access_control_handle_scan, access_control_handle_emergency_btn);

    /* 4. Distributed Network & Transport */
    cocis_wifi_init();
    cocis_mqtt_init(NODE_ID_ENTRANCE);
    net_protocol_init();
    light_init();
    pir_sensor_init();

    /* 5. Serial Interactive Console */
    cli_init();

    ESP_LOGI(TAG, "Entrance Hub fully initialized and listening for room telemetry");
}

void loop(void)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}

#ifndef __cplusplus
/* Export C++ mangled symbols expected by Arduino main.cpp when compiled with C compiler */
void _Z5setupv(void)
{
    setup();
}

void _Z4loopv(void)
{
    loop();
}
#endif
