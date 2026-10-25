#ifndef ROOM_BUFFER_H
#define ROOM_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "cocis_types.h"
#include "cocis_config.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t room_buffer_init(void);

esp_err_t room_buffer_push(const proto_packet_t *pkt);

bool room_buffer_peek(proto_packet_t *out_pkt);

void room_buffer_pop(void);

size_t room_buffer_count(void);

bool room_buffer_is_empty(void);

#ifdef __cplusplus
}
#endif

#endif /* ROOM_BUFFER_H */
