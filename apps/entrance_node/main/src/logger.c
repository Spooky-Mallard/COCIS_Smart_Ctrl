#include "logger.h"
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "storage_sd.h"
#include "time_sync.h"

static const char *TAG = "LOGGER";

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
