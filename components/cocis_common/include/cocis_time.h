#ifndef COCIS_TIME_H
#define COCIS_TIME_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cocis_time_init(void);

int64_t cocis_time_now_epoch(void);

void cocis_time_format_now(char *buf, size_t len);

void cocis_time_format_epoch(int64_t epoch, char *buf, size_t len);

bool cocis_time_is_synced(void);

void cocis_time_set_epoch(int64_t epoch);

#ifdef __cplusplus
}
#endif

#endif /* COCIS_TIME_H */
