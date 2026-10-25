#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include "esp_err.h"

esp_err_t display_init(void);
esp_err_t display_show_alert(const char *line0, const char *line1, uint32_t duration_ms);

#endif
