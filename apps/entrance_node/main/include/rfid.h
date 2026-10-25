#ifndef RFID_H
#define RFID_H

#include <stdbool.h>
#include <stdint.h>
#include "config.h"
#include "esp_err.h"

typedef enum {
    RFID_READER_ENTRY = 0,
    RFID_READER_EXIT = 1
} rfid_reader_t;

typedef struct {
    rfid_reader_t reader;
    char          uid[CFG_RFID_UID_MAX_LEN];
    bool          from_hardware_btn;
} rfid_scan_event_t;

typedef void (*rfid_scan_callback_t)(const rfid_scan_event_t *evt);
typedef void (*rfid_emergency_button_cb_t)(void);

esp_err_t rfid_init(rfid_scan_callback_t scan_cb, rfid_emergency_button_cb_t emerg_cb);
esp_err_t rfid_inject_scan(rfid_reader_t reader, const char *uid);

#endif
