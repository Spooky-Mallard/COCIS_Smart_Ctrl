#ifndef CLI_H
#define CLI_H

#include "esp_err.h"

esp_err_t cli_init(void);
esp_err_t cli_execute_line(char *line);

#endif
