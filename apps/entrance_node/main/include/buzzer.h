#ifndef BUZZER_H
#define BUZZER_H

#include "esp_err.h"

typedef enum {
    BUZZER_PAT_GRANT = 0,
    BUZZER_PAT_DENY,
    BUZZER_PAT_ANTIPASSBACK,
    BUZZER_PAT_DOOR_ALARM,
    BUZZER_PAT_EMERGENCY
} buzzer_pattern_t;

esp_err_t buzzer_init(void);
esp_err_t buzzer_play(buzzer_pattern_t pattern);

#endif
