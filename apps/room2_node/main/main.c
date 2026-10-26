/**
 * ==============================================================================
 * CoCIS Smart Control System - Lecture Room 2 Terminal
 * College of Computing and Information Sciences (CoCIS), Makerere University
 * Single-file standalone firmware for Wokwi Web simulation
 * Node ID: NODE_ID_ROOM_2 | PIR: GPIO 26 | Relay Light: GPIO 25
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
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psa/crypto.h"

/* ============================================================================== */
/* 1. CONSTANTS, ENUMS & SYSTEM CONFIGURATION                                     */
/* ============================================================================== */

#define PIN_PIR                     GPIO_NUM_26
#define PIN_RELAY                   GPIO_NUM_25
#define PIN_STATUS_LED              GPIO_NUM_2
#define MY_NODE_ID                  NODE_ID_ROOM_2
#define MY_ROOM_ID                  2
#define ROOM_NAME_STR               "Lecture Room 2"

#define CFG_WIFI_SSID               "Wokwi-GUEST"
#define CFG_WIFI_PASS               ""
#define CFG_MQTT_BROKER_HOST        "broker.hivemq.com"
#define CFG_MQTT_BROKER_PORT        1883
#define CFG_MQTT_TOPIC_PREFIX       "cocis/smart_ctrl_mak"
#define CFG_SECRET_KEY              "cocis_secret_key_2026"
#define CFG_HEARTBEAT_INTERVAL_SEC  5
#define CFG_LOCK_TIMEOUT_MS         1000
#define CFG_MAX_STORE_FORWARD_EVENTS 32
#define CFG_TIMEZONE                "EAT-3"
#define CFG_SNTP_SERVER             "pool.ntp.org"
#define CFG_NODE_OFFLINE_TIMEOUT_SEC 15

#define GRACE_PERIOD_SEC            5
#define VACANCY_TIMEOUT_SEC         15
#define FIXTURE_WATTS               120

typedef enum {
    NODE_ID_ENTRANCE  = 0,
    NODE_ID_ROOM_1    = 1,
    NODE_ID_ROOM_2    = 2,
    NODE_ID_COUNT     = 3,
    NODE_ID_BROADCAST = 0xFF
} node_id_t;

typedef enum {
    LIGHT_STATE_OFF_VACANT    = 0,
    LIGHT_STATE_ON_ACTIVE     = 1,
    LIGHT_STATE_ON_GRACE_HOLD = 2
} light_state_t;

typedef enum {
    PROTO_MSG_HEARTBEAT       = 1,
    PROTO_MSG_ROOM_EVENT      = 2,
    PROTO_MSG_ACK             = 3,
    PROTO_MSG_LIGHT_OVERRIDE  = 4,
    PROTO_MSG_EMERGENCY       = 5,
    PROTO_MSG_SCHEDULE_SYNC   = 6
} proto_msg_type_t;

typedef struct {
    uint8_t   version;
    uint8_t   msg_type;
    uint8_t   src_node;
    uint8_t   dst_node;
    uint32_t  seq_id;
    uint32_t  ack_seq_id;
    int64_t   timestamp_epoch;
    uint8_t   room_id;
    uint8_t   occupied;
    uint8_t   light_state;
    uint8_t   emergency_active;
    uint32_t  light_on_sec;
    uint32_t  energy_saved_mwh;
    uint32_t  uptime_sec;
    char      hmac[65];
} proto_packet_t;

typedef enum {
    COCIS_LOCK_LEVEL_NETWORK = 0,
    COCIS_LOCK_LEVEL_ROOM    = 1,
    COCIS_LOCK_LEVEL_STORAGE = 2,
    COCIS_LOCK_LEVEL_DISPLAY = 3,
    COCIS_LOCK_LEVEL_COUNT   = 4
} cocis_lock_level_t;

#define COCIS_EVT_WIFI_CONNECTED    (1 << 0)
#define COCIS_EVT_MQTT_CONNECTED    (1 << 1)
#define COCIS_EVT_TIME_SYNCED       (1 << 2)

typedef struct {
    node_id_t node_id;
    const char *name;
    bool      heartbeat_healthy;
    bool      link_simulated_online;
    uint32_t  last_rx_seq;
    uint32_t  last_tx_seq;
    uint32_t  acked_count;
    uint32_t  dup_suppressed_count;
    int64_t   last_seen_uptime_sec;
    int8_t    rssi;
    uint32_t  buffered_offline_count;
    uint32_t  replayed_count;
} node_net_status_t;

typedef void (*cocis_mqtt_rx_cb_t)(const char *topic, const char *payload);
typedef void (*pir_motion_cb_t)(bool motion_active);
typedef void (*light_state_change_cb_t)(light_state_t new_state, bool occupied, uint32_t on_sec, uint32_t saved_mwh);

/* Global Tag for All Logging */
static const char *TAG = "ROOM2_NODE";

/* ============================================================================== */
/* 2. FORWARD DECLARATIONS                                                        */
/* ============================================================================== */

esp_err_t cocis_sync_init(void);
esp_err_t cocis_sync_lock(cocis_lock_level_t level, uint32_t timeout_ms);
void      cocis_sync_unlock(cocis_lock_level_t level);
void      cocis_sync_event_set(uint32_t bits);
void      cocis_sync_event_clear(uint32_t bits);
bool      cocis_sync_event_check(uint32_t bits);

esp_err_t cocis_crypto_init(void);
esp_err_t cocis_crypto_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out_mac[32]);
esp_err_t cocis_crypto_hmac_hex(const char *key, const char *payload, char out_hex[65]);
bool      cocis_crypto_verify_hmac(const char *key, const char *payload, const char *expected_hex);

esp_err_t cocis_time_init(void);
int64_t   cocis_time_now_epoch(void);
uint32_t  cocis_uptime_sec(void);
void      cocis_time_format_epoch(int64_t epoch, char *buf, size_t len);

esp_err_t cocis_health_init(node_id_t my_node_id);
void      cocis_health_record_rx(node_id_t src_node, uint32_t seq_id, uint32_t uptime_sec, int8_t rssi);

esp_err_t cocis_proto_init(void);
uint32_t  cocis_proto_next_seq(node_id_t my_node_id);
esp_err_t cocis_proto_serialize(const proto_packet_t *pkt, const char *secret_key, char *out_buf, size_t max_len);
esp_err_t cocis_proto_deserialize(const char *json_str, const char *secret_key, proto_packet_t *out_pkt);
bool      cocis_proto_is_duplicate(node_id_t src_node, uint32_t seq_id);
void      cocis_proto_record_seq(node_id_t src_node, uint32_t seq_id);

esp_err_t cocis_wifi_init(void);
bool      cocis_wifi_is_connected(void);
int8_t    cocis_wifi_get_rssi(void);
void      cocis_wifi_get_ip_str(char *buf, size_t len);

esp_err_t cocis_mqtt_init(node_id_t my_node_id);
bool      cocis_mqtt_is_connected(void);
esp_err_t cocis_mqtt_publish(const char *topic, const char *payload);
esp_err_t cocis_mqtt_subscribe(const char *topic, cocis_mqtt_rx_cb_t callback);

esp_err_t pir_sensor_init(gpio_num_t pin, pir_motion_cb_t cb);
bool      pir_sensor_is_motion_active(void);
void      pir_sensor_simulate(bool active);

esp_err_t light_init(gpio_num_t relay_pin, light_state_change_cb_t cb);
void      light_handle_motion(bool motion_active);
void      light_tick_1hz(void);
void      light_set_override(uint8_t override_val);
light_state_t light_get_state(void);
bool      light_is_relay_on(void);
uint32_t  light_get_on_seconds(void);
uint32_t  light_get_energy_saved_mwh(void);
const char *light_state_to_str(light_state_t state);

esp_err_t room_buffer_init(void);
esp_err_t room_buffer_push(const proto_packet_t *pkt);
bool      room_buffer_peek(proto_packet_t *out_pkt);
void      room_buffer_pop(void);
size_t    room_buffer_count(void);
bool      room_buffer_is_empty(void);

esp_err_t room_cli_init(void);

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
/* Module: cocis_sync                                                             */
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

/* ------------------------------------------------------------------------------ */
/* Module: cocis_crypto (HMAC-SHA256 via PSA Crypto)                             */
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
        ipad[i] = (uint8_t)(k_block[i] ^ 0x36);
        opad[i] = (uint8_t)(k_block[i] ^ 0x5C);
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

    int diff = 0;
    for (int i = 0; i < 64; i++) {
        diff |= (computed_hex[i] ^ expected_hex[i]);
    }
    return (diff == 0);
}

/* ------------------------------------------------------------------------------ */
/* Module: cocis_time & cocis_health                                              */
/* ------------------------------------------------------------------------------ */
static bool s_time_synced = false;

uint32_t cocis_uptime_sec(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

int64_t cocis_time_now_epoch(void)
{
    return (int64_t)time(NULL);
}

void cocis_time_format_epoch(int64_t epoch, char *buf, size_t len)
{
    time_t t = (time_t)epoch;
    struct tm tm_info;
    localtime_r(&t, &tm_info);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm_info);
}

static void sntp_sync_cb(struct timeval *tv)
{
    (void)tv;
    s_time_synced = true;
    cocis_sync_event_set(COCIS_EVT_TIME_SYNCED);
    char buf[32];
    cocis_time_format_epoch(cocis_time_now_epoch(), buf, sizeof(buf));
    ESP_LOGI(TAG, "SNTP synchronized network time: %s EAT", buf);
}

esp_err_t cocis_time_init(void)
{
    setenv("TZ", CFG_TIMEZONE, 1);
    tzset();

    struct timeval tv_seed = {
        .tv_sec = 1792904400, /* 2026-10-25 08:00:00 EAT */
        .tv_usec = 0
    };
    settimeofday(&tv_seed, NULL);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CFG_SNTP_SERVER);
    config.sync_cb = sntp_sync_cb;
    esp_netif_sntp_init(&config);

    char buf[32];
    cocis_time_format_epoch(cocis_time_now_epoch(), buf, sizeof(buf));
    ESP_LOGI(TAG, "Time subsystem initialized (Current seed: %s)", buf);
    return ESP_OK;
}

static node_id_t s_health_my_id = NODE_ID_ROOM_2;
static node_net_status_t s_health_nodes[NODE_ID_COUNT];
static bool s_health_initialized = false;

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
    if (s_health_initialized) {
        return ESP_OK;
    }
    s_health_my_id = my_node_id;
    memset(s_health_nodes, 0, sizeof(s_health_nodes));

    for (int i = 0; i < NODE_ID_COUNT; i++) {
        s_health_nodes[i].last_seen_uptime_sec = 0;
        s_health_nodes[i].heartbeat_healthy = ((node_id_t)i == my_node_id);
        s_health_nodes[i].rssi = -127;
    }

    s_health_initialized = true;
    xTaskCreate(health_monitor_task, "health_mon", 2048, NULL, 4, NULL);
    ESP_LOGI(TAG, "Health monitor initialized for Node %d", (int)my_node_id);
    return ESP_OK;
}

void cocis_health_record_rx(node_id_t src_node, uint32_t seq_id, uint32_t uptime_sec, int8_t rssi)
{
    (void)uptime_sec;
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

/* ------------------------------------------------------------------------------ */
/* Module: cocis_protocol                                                         */
/* ------------------------------------------------------------------------------ */
static uint32_t s_proto_next_seq[NODE_ID_COUNT] = { 1, 1, 1 };
static uint32_t s_proto_last_seen_seq[NODE_ID_COUNT] = { 0, 0, 0 };

esp_err_t cocis_proto_init(void)
{
    cocis_crypto_init();
    for (int i = 0; i < NODE_ID_COUNT; i++) {
        s_proto_next_seq[i] = 1;
        s_proto_last_seen_seq[i] = 0;
    }
    ESP_LOGI(TAG, "Protocol engine initialized with HMAC-SHA256 support");
    return ESP_OK;
}

uint32_t cocis_proto_next_seq(node_id_t my_node_id)
{
    if (my_node_id >= NODE_ID_COUNT) {
        return 0;
    }
    uint32_t seq = s_proto_next_seq[my_node_id]++;
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

    if (body_len <= 0 || body_len >= (int)sizeof(body)) {
        return ESP_ERR_NO_MEM;
    }

    char hmac_hex[65] = { 0 };
    esp_err_t err = cocis_crypto_hmac_hex(secret_key ? secret_key : CFG_SECRET_KEY, body, hmac_hex);
    if (err != ESP_OK) {
        return err;
    }

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

    const char *hmac_tag = ",\"hmac\":\"";
    const char *p_hmac = strstr(json_str, hmac_tag);
    if (!p_hmac) {
        ESP_LOGW(TAG, "Packet missing mandatory HMAC signature!");
        return ESP_ERR_INVALID_CRC;
    }

    char received_hmac[65] = { 0 };
    if (sscanf(p_hmac + strlen(hmac_tag), "%64[0-9a-fA-F]", received_hmac) != 1) {
        ESP_LOGW(TAG, "Malformed HMAC signature");
        return ESP_ERR_INVALID_CRC;
    }

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
    return (seq_id <= s_proto_last_seen_seq[src_node]);
}

void cocis_proto_record_seq(node_id_t src_node, uint32_t seq_id)
{
    if (src_node < NODE_ID_COUNT && seq_id > s_proto_last_seen_seq[src_node]) {
        s_proto_last_seen_seq[src_node] = seq_id;
    }
}

/* ------------------------------------------------------------------------------ */
/* Module: cocis_wifi                                                             */
/* ------------------------------------------------------------------------------ */
static bool s_wifi_connected = false;
static int s_wifi_retry_count = 0;
static char s_wifi_ip_str[16] = "0.0.0.0";
static esp_netif_t *s_wifi_netif = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
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
    esp_netif_init();
    esp_event_loop_create_default();
    s_wifi_netif = esp_netif_create_default_wifi_sta();
    (void)s_wifi_netif;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    strncpy((char *)wifi_config.sta.ssid, CFG_WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CFG_WIFI_PASS, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();

    ESP_LOGI(TAG, "Wi-Fi station subsystem initialized");
    return ESP_OK;
}

bool cocis_wifi_is_connected(void)
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
    return -55;
}

void cocis_wifi_get_ip_str(char *buf, size_t len)
{
    if (buf && len > 0) {
        strncpy(buf, s_wifi_ip_str, len - 1);
        buf[len - 1] = '\0';
    }
}

/* ------------------------------------------------------------------------------ */
/* Module: cocis_mqtt (Raw TCP client with 10KB stack & multi-byte length decoder) */
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
static node_id_t s_mqtt_my_node_id = NODE_ID_ROOM_2;
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
    size_t rem_len = 2 + 2 + tlen + 1;

    pkt[0] = 0x82; /* SUBSCRIBE QoS 1 */
    pkt[1] = (uint8_t)rem_len;
    pkt[2] = 0x00; pkt[3] = 0x01; /* Packet ID */
    pkt[4] = (uint8_t)(tlen >> 8);
    pkt[5] = (uint8_t)(tlen & 0xFF);
    memcpy(&pkt[6], topic, tlen);
    pkt[6 + tlen] = 0x00;

    if (mqtt_send_bytes(pkt, 7 + tlen) == ESP_OK) {
        ESP_LOGI(TAG, "Subscribed to MQTT topic: %s", topic);
        return ESP_OK;
    }
    return ESP_FAIL;
}

static bool topic_matches(const char *sub, const char *topic)
{
    if (!sub || !topic) return false;
    if (strcmp(sub, topic) == 0) return true;

    const char *hash = strstr(sub, "/#");
    if (hash) {
        size_t prefix_len = (size_t)(hash - sub);
        if (strncmp(sub, topic, prefix_len) == 0) return true;
    }

    const char *plus = strstr(sub, "/+");
    if (plus) {
        size_t prefix_len = (size_t)(plus - sub);
        if (strncmp(sub, topic, prefix_len) != 0) return false;
        const char *sub_suffix = plus + 2;
        if (*sub_suffix == '\0') return true;
        const char *topic_rem = topic + prefix_len + 1;
        const char *next_slash = strchr(topic_rem, '/');
        if (next_slash && strcmp(next_slash, sub_suffix) == 0) return true;
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
        if (!cocis_wifi_is_connected()) {
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
            struct addrinfo hints;
            memset(&hints, 0, sizeof(hints));
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
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

            uint16_t cid_len = (uint16_t)strlen(s_mqtt_client_id);
            uint8_t conn_pkt[128];
            size_t rem_len = 10 + 2 + cid_len;

            conn_pkt[0] = 0x10;
            conn_pkt[1] = (uint8_t)rem_len;
            conn_pkt[2] = 0x00; conn_pkt[3] = 0x04;
            conn_pkt[4] = 'M';  conn_pkt[5] = 'Q'; conn_pkt[6] = 'T'; conn_pkt[7] = 'T';
            conn_pkt[8] = 0x04;
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

            uint8_t connack[4] = { 0 };
            int n = recv(s, connack, sizeof(connack), 0);
            if (n == 4 && connack[0] == 0x20 && connack[3] == 0x00) {
                s_mqtt_sock = s;
                s_mqtt_connected = true;
                cocis_sync_event_set(COCIS_EVT_MQTT_CONNECTED);
                ESP_LOGI(TAG, "MQTT connected to %s:%d (Client: %s)",
                         CFG_MQTT_BROKER_HOST, CFG_MQTT_BROKER_PORT, s_mqtt_client_id);

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

        int r = recv(s_mqtt_sock, s_mqtt_rx_buf, sizeof(s_mqtt_rx_buf) - 1, 0);
        if (r > 0) {
            s_mqtt_rx_buf[r] = '\0';
            uint8_t pkt_type = s_mqtt_rx_buf[0] & 0xF0;
            if (pkt_type == 0x30) { /* PUBLISH */
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

                size_t total_needed = idx + rem_len;
                while ((size_t)r < total_needed && (size_t)r < sizeof(s_mqtt_rx_buf) - 1) {
                    int extra = recv(s_mqtt_sock, s_mqtt_rx_buf + r, sizeof(s_mqtt_rx_buf) - 1 - r, 0);
                    if (extra <= 0) break;
                    r += extra;
                    s_mqtt_rx_buf[r] = '\0';
                }

                if (idx + 2 <= (size_t)r) {
                    uint16_t tlen = ((uint16_t)s_mqtt_rx_buf[idx] << 8) | s_mqtt_rx_buf[idx + 1];
                    idx += 2;
                    if (idx + tlen <= (size_t)r) {
                        char topic[64];
                        size_t cpy_tlen = (tlen < sizeof(topic) - 1) ? tlen : (sizeof(topic) - 1);
                        memcpy(topic, &s_mqtt_rx_buf[idx], cpy_tlen);
                        topic[cpy_tlen] = '\0';
                        idx += tlen;

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

bool cocis_mqtt_is_connected(void)
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
/* Module: pir_sensor (GPIO Interrupt + Queue Worker Task)                       */
/* ------------------------------------------------------------------------------ */
static gpio_num_t s_pir_pin = PIN_PIR;
static pir_motion_cb_t s_pir_callback = NULL;
static QueueHandle_t s_pir_queue = NULL;
static bool s_pir_motion_state = false;

static void IRAM_ATTR pir_isr_handler(void *arg)
{
    (void)arg;
    int level = gpio_get_level(s_pir_pin);
    BaseType_t high_task_woken = pdFALSE;
    if (s_pir_queue != NULL) {
        xQueueSendFromISR(s_pir_queue, &level, &high_task_woken);
    }
    portYIELD_FROM_ISR(high_task_woken);
}

static void pir_task(void *arg)
{
    (void)arg;
    int level = 0;
    int64_t last_change_ms = 0;

    while (1) {
        if (xQueueReceive(s_pir_queue, &level, portMAX_DELAY) == pdTRUE) {
            int64_t now_ms = esp_timer_get_time() / 1000LL;
            if (now_ms - last_change_ms < 100) {
                continue;
            }
            last_change_ms = now_ms;
            bool new_motion = (level != 0);
            if (new_motion != s_pir_motion_state) {
                s_pir_motion_state = new_motion;
                ESP_LOGI(TAG, "PIR state changed -> %s (GPIO %d)",
                         s_pir_motion_state ? "MOTION ACTIVE" : "MOTION CLEARED", (int)s_pir_pin);
                if (s_pir_callback) {
                    s_pir_callback(s_pir_motion_state);
                }
            }
        }
    }
}

esp_err_t pir_sensor_init(gpio_num_t pin, pir_motion_cb_t cb)
{
    s_pir_pin = pin;
    s_pir_callback = cb;

    if (s_pir_queue == NULL) {
        s_pir_queue = xQueueCreate(16, sizeof(int));
        if (s_pir_queue == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    gpio_config_t io_conf;
    memset(&io_conf, 0, sizeof(io_conf));
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    io_conf.intr_type = GPIO_INTR_ANYEDGE;

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    gpio_install_isr_service(0);
    gpio_isr_handler_add(pin, pir_isr_handler, NULL);

    s_pir_motion_state = (gpio_get_level(pin) != 0);

    if (xTaskCreate(pir_task, "pir_task", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "PIR motion sensor task initialized on GPIO %d (initial state=%d)",
             (int)pin, (int)s_pir_motion_state);
    return ESP_OK;
}

bool pir_sensor_is_motion_active(void)
{
    return s_pir_motion_state;
}

void pir_sensor_simulate(bool active)
{
    int lvl = active ? 1 : 0;
    if (s_pir_queue != NULL) {
        xQueueSend(s_pir_queue, &lvl, pdMS_TO_TICKS(50));
    }
}

/* ------------------------------------------------------------------------------ */
/* Module: light (FSM, Relay GPIO & Energy Accounting)                            */
/* ------------------------------------------------------------------------------ */
static gpio_num_t s_light_relay_pin = PIN_RELAY;
static light_state_change_cb_t s_light_callback = NULL;
static light_state_t s_light_state = LIGHT_STATE_OFF_VACANT;
static bool s_light_relay_on = false;
static bool s_light_motion_active = false;
static uint8_t s_light_override_mode = 2; /* 0=force OFF, 1=force ON, 2=AUTO */
static int32_t s_light_countdown_sec = 0;
static uint32_t s_light_total_on_sec = 0;
static uint32_t s_light_total_vacant_sec = 0;

const char *light_state_to_str(light_state_t state)
{
    switch (state) {
    case LIGHT_STATE_OFF_VACANT:    return "OFF_VACANT";
    case LIGHT_STATE_ON_ACTIVE:     return "ON_ACTIVE";
    case LIGHT_STATE_ON_GRACE_HOLD: return "ON_GRACE_HOLD";
    default:                        return "UNKNOWN";
    }
}

static void apply_relay(bool on)
{
    s_light_relay_on = on;
    gpio_set_level(s_light_relay_pin, on ? 1 : 0);
}

esp_err_t light_init(gpio_num_t relay_pin, light_state_change_cb_t cb)
{
    s_light_relay_pin = relay_pin;
    s_light_callback = cb;

    gpio_config_t io_conf;
    memset(&io_conf, 0, sizeof(io_conf));
    io_conf.pin_bit_mask = (1ULL << relay_pin);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    apply_relay(false);
    s_light_state = LIGHT_STATE_OFF_VACANT;

    ESP_LOGI(TAG, "Lecture room lighting FSM initialized on GPIO %d", (int)relay_pin);
    return ESP_OK;
}

void light_handle_motion(bool motion_active)
{
    s_light_motion_active = motion_active;

    if (s_light_override_mode == 0) {
        apply_relay(false);
        return;
    }
    if (s_light_override_mode == 1) {
        apply_relay(true);
        return;
    }

    /* AUTO Mode */
    if (motion_active) {
        if (s_light_state != LIGHT_STATE_ON_ACTIVE) {
            s_light_state = LIGHT_STATE_ON_ACTIVE;
            apply_relay(true);
            ESP_LOGI(TAG, "FSM: Motion detected -> Transitioned to ON_ACTIVE");
            if (s_light_callback) {
                s_light_callback(s_light_state, true, s_light_total_on_sec, light_get_energy_saved_mwh());
            }
        }
        s_light_countdown_sec = 0;
    } else {
        if (s_light_state == LIGHT_STATE_ON_ACTIVE) {
            s_light_state = LIGHT_STATE_ON_GRACE_HOLD;
            s_light_countdown_sec = GRACE_PERIOD_SEC + VACANCY_TIMEOUT_SEC;
            ESP_LOGI(TAG, "FSM: Motion cleared -> Entering GRACE_HOLD (%ld s countdown)", (long)s_light_countdown_sec);
        }
    }
}

void light_tick_1hz(void)
{
    if (s_light_relay_on) {
        s_light_total_on_sec++;
    } else {
        s_light_total_vacant_sec++;
    }

    if (s_light_override_mode != 2) {
        return;
    }

    if (s_light_state == LIGHT_STATE_ON_GRACE_HOLD) {
        if (s_light_countdown_sec > 0) {
            s_light_countdown_sec--;
        }
        if (s_light_countdown_sec == 0) {
            s_light_state = LIGHT_STATE_OFF_VACANT;
            apply_relay(false);
            ESP_LOGI(TAG, "FSM: Vacancy timeout reached -> Lights switched OFF (Energy saved: %lu mWh)",
                     (unsigned long)light_get_energy_saved_mwh());
            if (s_light_callback) {
                s_light_callback(s_light_state, false, s_light_total_on_sec, light_get_energy_saved_mwh());
            }
        }
    }
}

void light_set_override(uint8_t override_val)
{
    s_light_override_mode = override_val;
    if (override_val == 0) {
        apply_relay(false);
        s_light_state = LIGHT_STATE_OFF_VACANT;
        ESP_LOGI(TAG, "Remote OVERRIDE: Lights forced OFF");
    } else if (override_val == 1) {
        apply_relay(true);
        s_light_state = LIGHT_STATE_ON_ACTIVE;
        ESP_LOGI(TAG, "Remote OVERRIDE: Lights forced ON");
    } else {
        ESP_LOGI(TAG, "Remote OVERRIDE: Released back to AUTO control");
        light_handle_motion(s_light_motion_active);
    }
}

light_state_t light_get_state(void)
{
    return s_light_state;
}

bool light_is_relay_on(void)
{
    return s_light_relay_on;
}

uint32_t light_get_on_seconds(void)
{
    return s_light_total_on_sec;
}

uint32_t light_get_energy_saved_mwh(void)
{
    return (uint32_t)((uint64_t)s_light_total_vacant_sec * FIXTURE_WATTS * 1000ULL / 3600ULL);
}

/* ------------------------------------------------------------------------------ */
/* Module: room_buffer (Store-and-Forward FIFO Ring Buffer)                       */
/* ------------------------------------------------------------------------------ */
static proto_packet_t s_room_buffer[CFG_MAX_STORE_FORWARD_EVENTS];
static size_t s_room_buf_head = 0;
static size_t s_room_buf_tail = 0;
static size_t s_room_buf_count = 0;
static SemaphoreHandle_t s_room_buf_mutex = NULL;

esp_err_t room_buffer_init(void)
{
    s_room_buf_mutex = xSemaphoreCreateMutex();
    if (!s_room_buf_mutex) {
        return ESP_ERR_NO_MEM;
    }
    s_room_buf_head = 0;
    s_room_buf_tail = 0;
    s_room_buf_count = 0;
    ESP_LOGI(TAG, "Store-and-forward FIFO ring buffer initialized (cap=%d)", CFG_MAX_STORE_FORWARD_EVENTS);
    return ESP_OK;
}

esp_err_t room_buffer_push(const proto_packet_t *pkt)
{
    if (!pkt || !s_room_buf_mutex) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_room_buf_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_room_buf_count >= CFG_MAX_STORE_FORWARD_EVENTS) {
        s_room_buf_head = (s_room_buf_head + 1) % CFG_MAX_STORE_FORWARD_EVENTS;
        s_room_buf_count--;
        ESP_LOGW(TAG, "Ring buffer full! Dropped oldest entry to maintain FIFO order.");
    }

    s_room_buffer[s_room_buf_tail] = *pkt;
    s_room_buf_tail = (s_room_buf_tail + 1) % CFG_MAX_STORE_FORWARD_EVENTS;
    s_room_buf_count++;

    ESP_LOGI(TAG, "Buffered packet seq=%lu (depth=%u/%u)",
             (unsigned long)pkt->seq_id, (unsigned)s_room_buf_count, CFG_MAX_STORE_FORWARD_EVENTS);

    xSemaphoreGive(s_room_buf_mutex);
    return ESP_OK;
}

bool room_buffer_peek(proto_packet_t *out_pkt)
{
    if (!out_pkt || !s_room_buf_mutex) {
        return false;
    }

    if (xSemaphoreTake(s_room_buf_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }

    if (s_room_buf_count == 0) {
        xSemaphoreGive(s_room_buf_mutex);
        return false;
    }

    *out_pkt = s_room_buffer[s_room_buf_head];
    xSemaphoreGive(s_room_buf_mutex);
    return true;
}

void room_buffer_pop(void)
{
    if (!s_room_buf_mutex) {
        return;
    }

    if (xSemaphoreTake(s_room_buf_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (s_room_buf_count > 0) {
            s_room_buf_head = (s_room_buf_head + 1) % CFG_MAX_STORE_FORWARD_EVENTS;
            s_room_buf_count--;
        }
        xSemaphoreGive(s_room_buf_mutex);
    }
}

size_t room_buffer_count(void)
{
    size_t count = 0;
    if (s_room_buf_mutex && xSemaphoreTake(s_room_buf_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        count = s_room_buf_count;
        xSemaphoreGive(s_room_buf_mutex);
    }
    return count;
}

bool room_buffer_is_empty(void)
{
    return (room_buffer_count() == 0);
}

/* ------------------------------------------------------------------------------ */
/* Module: room_cli (Interactive Serial Console)                                  */
/* ------------------------------------------------------------------------------ */
static void print_status(void)
{
    char ip[16] = { 0 };
    cocis_wifi_get_ip_str(ip, sizeof(ip));

    printf("\n==================================================\n");
    printf("         LECTURE ROOM %u TERMINAL DIAGNOSTICS      \n", MY_ROOM_ID);
    printf("==================================================\n");
    printf(" PIR Sensor Input:      %s\n", pir_sensor_is_motion_active() ? "MOTION DETECTED" : "IDLE (VACANT)");
    printf(" Lighting Fixture:      %s\n", light_is_relay_on() ? "RELAY ON (LIGHTS POWERED)" : "RELAY OFF");
    printf(" FSM State:             %s\n", light_state_to_str(light_get_state()));
    printf(" Cumulative Active:     %lu sec\n", (unsigned long)light_get_on_seconds());
    printf(" Energy Saved:          %lu mWh\n", (unsigned long)light_get_energy_saved_mwh());
    printf(" Store & Forward Queue: %u / 32 entries\n", (unsigned)room_buffer_count());
    printf(" Wi-Fi Status:          %s (IP: %s, RSSI: %d dBm)\n",
           cocis_wifi_is_connected() ? "CONNECTED" : "OFFLINE", ip, (int)cocis_wifi_get_rssi());
    printf(" MQTT Broker Session:   %s\n", cocis_mqtt_is_connected() ? "ONLINE" : "DISCONNECTED");
    printf(" System Uptime:         %lu sec\n", (unsigned long)cocis_uptime_sec());
    printf(" Free Heap:             %u bytes\n", (unsigned)esp_get_free_heap_size());
    printf("==================================================\n\n");
}

static void cli_task(void *arg)
{
    (void)arg;
    char line[64];
    size_t pos = 0;

    printf("\n[Room %u Terminal] Diagnostic console ready. Type 'status', 'motion', 'clear', or 'help'.\nroom%u> ", MY_ROOM_ID, MY_ROOM_ID);
    fflush(stdout);

    while (1) {
        int c = getchar();
        if (c == EOF || c < 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (c == '\r' || c == '\n') {
            if (pos > 0) {
                line[pos] = '\0';
                printf("\n");

                if (strcmp(line, "status") == 0) {
                    print_status();
                } else if (strcmp(line, "motion") == 0) {
                    pir_sensor_simulate(true);
                    printf("OK: Simulated motion trigger injected\n");
                } else if (strcmp(line, "clear") == 0) {
                    pir_sensor_simulate(false);
                    printf("OK: Simulated motion cleared\n");
                } else if (strcmp(line, "help") == 0) {
                    printf("Available commands:\n");
                    printf("  status - Display room sensors, relay, buffer depth, and network health\n");
                    printf("  motion - Simulate PIR motion detection\n");
                    printf("  clear  - Clear PIR motion detection (start vacancy countdown)\n");
                    printf("  help   - Show this reference\n");
                } else {
                    printf("Unknown command: '%s'. Type 'help' for options.\n", line);
                }
                pos = 0;
            }
            printf("room%u> ", MY_ROOM_ID);
            fflush(stdout);
        } else if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
        } else if (pos < sizeof(line) - 1 && c >= 32 && c <= 126) {
            line[pos++] = (char)c;
            putchar(c);
            fflush(stdout);
        }
    }
}

esp_err_t room_cli_init(void)
{
    xTaskCreate(cli_task, "room_cli", 3072, NULL, 3, NULL);
    ESP_LOGI(TAG, "Diagnostic console task spawned");
    return ESP_OK;
}

/* ============================================================================== */
/* 4. MAIN ORCHESTRATION & MQTT CALLBACKS                                        */
/* ============================================================================== */

static uint32_t s_pending_ack_seq = 0;

static void send_or_buffer_packet(proto_packet_t *pkt)
{
    pkt->version = 1;
    pkt->src_node = MY_NODE_ID;
    pkt->dst_node = NODE_ID_ENTRANCE;
    pkt->seq_id = cocis_proto_next_seq(MY_NODE_ID);
    pkt->timestamp_epoch = cocis_time_now_epoch();
    pkt->room_id = MY_ROOM_ID;
    pkt->uptime_sec = cocis_uptime_sec();

    char buf[256];
    if (cocis_proto_serialize(pkt, CFG_SECRET_KEY, buf, sizeof(buf)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to serialize packet with HMAC");
        return;
    }

    if (cocis_mqtt_is_connected()) {
        char topic[64];
        snprintf(topic, sizeof(topic), "%s/room/%u/telemetry", CFG_MQTT_TOPIC_PREFIX, MY_ROOM_ID);
        if (cocis_mqtt_publish(topic, buf) == ESP_OK) {
            s_pending_ack_seq = pkt->seq_id;
            ESP_LOGI(TAG, "Published live event seq=%lu to Hub via MQTT", (unsigned long)pkt->seq_id);
            return;
        }
    }

    /* If offline or publish failed -> store and forward */
    room_buffer_push(pkt);
}

static void on_light_state_change(light_state_t new_state, bool occupied, uint32_t on_sec, uint32_t saved_mwh)
{
    proto_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.version = 1;
    pkt.msg_type = PROTO_MSG_ROOM_EVENT;
    pkt.src_node = MY_NODE_ID;
    pkt.dst_node = NODE_ID_ENTRANCE;
    pkt.room_id = MY_ROOM_ID;
    pkt.occupied = occupied ? 1 : 0;
    pkt.light_state = (uint8_t)new_state;
    pkt.light_on_sec = on_sec;
    pkt.energy_saved_mwh = saved_mwh;

    send_or_buffer_packet(&pkt);
}

static void on_pir_motion(bool motion_active)
{
    light_handle_motion(motion_active);
}

static void on_hub_control_message(const char *topic, const char *payload)
{
    (void)topic;
    proto_packet_t pkt;
    esp_err_t err = cocis_proto_deserialize(payload, CFG_SECRET_KEY, &pkt);
    if (err != ESP_OK) {
        if (strstr(payload, "\"cmd\":\"motion\"")) {
            int val = 0;
            if (sscanf(payload, "{\"cmd\":\"motion\",\"val\":%d}", &val) == 1) {
                pir_sensor_simulate(val != 0);
            }
        }
        return;
    }

    if (pkt.msg_type == PROTO_MSG_ACK) {
        if (pkt.ack_seq_id == s_pending_ack_seq) {
            s_pending_ack_seq = 0;
            ESP_LOGI(TAG, "Received ACK from Hub for live packet seq=%lu", (unsigned long)pkt.ack_seq_id);
        }

        proto_packet_t front;
        if (room_buffer_peek(&front) && front.seq_id == pkt.ack_seq_id) {
            room_buffer_pop();
            ESP_LOGI(TAG, "Replayed packet seq=%lu acknowledged and popped from buffer", (unsigned long)pkt.ack_seq_id);
        }
    } else if (pkt.msg_type == PROTO_MSG_LIGHT_OVERRIDE) {
        ESP_LOGW(TAG, "Received remote light override command from Entrance Hub: %u", (unsigned)pkt.light_state);
        light_set_override(pkt.light_state);
    } else if (pkt.msg_type == PROTO_MSG_EMERGENCY) {
        if (pkt.emergency_active) {
            ESP_LOGE(TAG, "EMERGENCY EVACUATION ACTIVE! Turning lights ON immediately for physical safety.");
            light_set_override(1);
        } else {
            ESP_LOGI(TAG, "Emergency cleared. Resuming AUTO lighting.");
            light_set_override(2);
        }
    }
}

/* ============================================================================== */
/* 5. ARDUINO-COMPATIBLE RUNNER (setup & loop for Wokwi Web)                      */
/* ============================================================================== */

void setup(void)
{
    printf("\n");
    printf("====================================================================\n");
    printf("     %s\n", "CoCIS SMART CONTROL SYSTEM - LECTURE ROOM 2 TERMINAL");
    printf("     Makerere University | BSE III & CS II Coursework Project 2026  \n");
    printf("     Role: %s | PIR: GPIO %d | Relay: GPIO %d           \n", "NODE_ID_ROOM_2", (int)PIN_PIR, (int)PIN_RELAY);
    printf("====================================================================\n\n");

    /* 1. System Synchronization, NVS & Cryptography */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    cocis_sync_init();
    cocis_crypto_init();
    cocis_time_init();
    cocis_health_init(MY_NODE_ID);
    cocis_proto_init();
    room_buffer_init();

    /* 2. Configure Status LED */
    gpio_config_t led_conf;
    memset(&led_conf, 0, sizeof(led_conf));
    led_conf.pin_bit_mask = (1ULL << PIN_STATUS_LED);
    led_conf.mode = GPIO_MODE_OUTPUT;
    led_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    led_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    led_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&led_conf);

    /* 3. Peripherals */
    light_init(PIN_RELAY, on_light_state_change);
    pir_sensor_init(PIN_PIR, on_pir_motion);

    /* 4. Networking & Remote Subscriptions */
    cocis_wifi_init();
    cocis_mqtt_init(MY_NODE_ID);

    char ctrl_topic[64];
    snprintf(ctrl_topic, sizeof(ctrl_topic), "%s/room/%u/control", CFG_MQTT_TOPIC_PREFIX, MY_ROOM_ID);
    cocis_mqtt_subscribe(ctrl_topic, on_hub_control_message);

    char bcast_topic[64];
    snprintf(bcast_topic, sizeof(bcast_topic), "%s/broadcast", CFG_MQTT_TOPIC_PREFIX);
    cocis_mqtt_subscribe(bcast_topic, on_hub_control_message);

    /* 5. Diagnostic Console */
    room_cli_init();

    ESP_LOGI(TAG, "Room %u terminal running autonomously. Store-and-forward active.", MY_ROOM_ID);
}

void loop(void)
{
    static uint32_t ticks = 0;
    vTaskDelay(pdMS_TO_TICKS(1000));
    ticks++;

    light_tick_1hz();

    /* Blink Status LED */
    gpio_set_level(PIN_STATUS_LED, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_STATUS_LED, 0);

    /* Send Heartbeat every 5 seconds */
    if (ticks % CFG_HEARTBEAT_INTERVAL_SEC == 0 && cocis_mqtt_is_connected()) {
        proto_packet_t hb;
        memset(&hb, 0, sizeof(hb));
        hb.version = 1;
        hb.msg_type = PROTO_MSG_HEARTBEAT;
        hb.src_node = MY_NODE_ID;
        hb.dst_node = NODE_ID_ENTRANCE;
        hb.seq_id = cocis_proto_next_seq(MY_NODE_ID);
        hb.timestamp_epoch = cocis_time_now_epoch();
        hb.room_id = MY_ROOM_ID;
        hb.occupied = pir_sensor_is_motion_active() ? 1 : 0;
        hb.light_state = (uint8_t)light_get_state();
        hb.light_on_sec = light_get_on_seconds();
        hb.energy_saved_mwh = light_get_energy_saved_mwh();
        hb.uptime_sec = cocis_uptime_sec();

        char hb_buf[256];
        if (cocis_proto_serialize(&hb, CFG_SECRET_KEY, hb_buf, sizeof(hb_buf)) == ESP_OK) {
            char telem_topic[64];
            snprintf(telem_topic, sizeof(telem_topic), "%s/room/%u/telemetry", CFG_MQTT_TOPIC_PREFIX, MY_ROOM_ID);
            cocis_mqtt_publish(telem_topic, hb_buf);
        }
    }

    /* Drain Store-and-Forward Buffer if online */
    if (cocis_mqtt_is_connected() && !room_buffer_is_empty()) {
        proto_packet_t front;
        if (room_buffer_peek(&front)) {
            char replay_buf[256];
            if (cocis_proto_serialize(&front, CFG_SECRET_KEY, replay_buf, sizeof(replay_buf)) == ESP_OK) {
                char telem_topic[64];
                snprintf(telem_topic, sizeof(telem_topic), "%s/room/%u/telemetry", CFG_MQTT_TOPIC_PREFIX, MY_ROOM_ID);
                cocis_mqtt_publish(telem_topic, replay_buf);
                ESP_LOGI(TAG, "Flushed buffered packet seq=%lu to Hub", (unsigned long)front.seq_id);
            }
        }
    }
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
