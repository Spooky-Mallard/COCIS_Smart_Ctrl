#ifndef COCIS_PROTOCOL_H
#define COCIS_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "cocis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cocis_proto_init(void);

uint32_t cocis_proto_next_seq(node_id_t my_node_id);

esp_err_t cocis_proto_serialize(const proto_packet_t *pkt, const char *secret_key,
                                char *out_buf, size_t max_len);

esp_err_t cocis_proto_deserialize(const char *json_str, const char *secret_key,
                                  proto_packet_t *out_pkt);

bool cocis_proto_is_duplicate(node_id_t src_node, uint32_t seq_id);

void cocis_proto_record_seq(node_id_t src_node, uint32_t seq_id);

#ifdef __cplusplus
}
#endif

#endif /* COCIS_PROTOCOL_H */
