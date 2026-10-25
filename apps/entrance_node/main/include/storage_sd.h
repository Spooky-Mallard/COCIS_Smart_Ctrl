#ifndef STORAGE_SD_H
#define STORAGE_SD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

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

#endif
