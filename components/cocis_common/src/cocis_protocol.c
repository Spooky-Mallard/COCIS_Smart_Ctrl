#include "cocis_protocol.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "cocis_crypto.h"
#include "cocis_config.h"
#include "cocis_sync.h"

static const char *TAG = "cocis_protocol";

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
    char *pos;
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
