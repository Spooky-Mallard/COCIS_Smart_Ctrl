#include "storage_sd.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include "board_pins.h"
#include "config.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "sync.h"

static const char *TAG = "STORAGE_SD";

static bool s_mounted = false;
static sdmmc_card_t *s_card = NULL;
static uint32_t s_write_ok = 0;
static uint32_t s_write_err = 0;

static void build_path(const char *rel, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s/%s", CFG_SD_MOUNT_POINT, rel);
}

static uint32_t get_file_size_unlocked(const char *rel_filename)
{
    char path[64];
    build_path(rel_filename, path, sizeof(path));
    struct stat st;
    if (stat(path, &st) == 0 && st.st_size > 0) {
        return (uint32_t)st.st_size;
    }
    return 0;
}

esp_err_t storage_sd_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_SD_MOSI,
        .miso_io_num = PIN_SD_MISO,
        .sclk_io_num = PIN_SD_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    esp_err_t err = spi_bus_initialize(BOARD_SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SPI bus init returned %s (continuing with RAM fallback)", esp_err_to_name(err));
        return err;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SD_SPI_HOST;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = PIN_SD_CS;
    slot_cfg.host_id = BOARD_SD_SPI_HOST;

    err = esp_vfs_fat_sdspi_mount(CFG_SD_MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        s_mounted = false;
        sync_event_clear(SYS_EVT_SD_MOUNTED);
        ESP_LOGW(TAG, "MicroSD not detected (%s) - operating in RAM ring-buffer mode", esp_err_to_name(err));
        return err;
    }

    s_mounted = true;
    sync_event_set(SYS_EVT_SD_MOUNTED);
    ESP_LOGI(TAG, "MicroSD mounted at %s (Card: %s)", CFG_SD_MOUNT_POINT, s_card ? s_card->cid.name : "SDSPI");
    return ESP_OK;
}

bool storage_sd_is_mounted(void)
{
    return s_mounted;
}

esp_err_t storage_sd_append_line(const char *rel_filename, const char *csv_header, const char *line)
{
    if (rel_filename == NULL || line == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_SD, 500);
    if (err != ESP_OK) {
        return err;
    }

    char path[64];
    build_path(rel_filename, path, sizeof(path));

    struct stat st;
    bool exists = (stat(path, &st) == 0);
    if (exists && st.st_size >= CFG_SD_MAX_FILE_BYTES) {
        char bak_path[72];
        snprintf(bak_path, sizeof(bak_path), "%s.old", path);
        unlink(bak_path);
        rename(path, bak_path);
        exists = false;
    }

    FILE *f = fopen(path, "a");
    if (f == NULL) {
        s_write_err++;
        sync_unlock(LOCK_LEVEL_SD);
        return ESP_FAIL;
    }

    if (!exists && csv_header != NULL) {
        fprintf(f, "%s\n", csv_header);
    }
    fprintf(f, "%s\n", line);
    fflush(f);
    fclose(f);

    s_write_ok++;
    sync_unlock(LOCK_LEVEL_SD);
    return ESP_OK;
}

esp_err_t storage_sd_read_lines(const char *rel_filename, sd_line_visitor_t visitor, void *ctx)
{
    if (rel_filename == NULL || visitor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_SD, 500);
    if (err != ESP_OK) {
        return err;
    }

    char path[64];
    build_path(rel_filename, path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        sync_unlock(LOCK_LEVEL_SD);
        return ESP_ERR_NOT_FOUND;
    }

    char line[256];
    bool first = true;
    while (fgets(line, sizeof(line), f) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (first && strncmp(line, "seq,", 4) == 0) {
            first = false;
            continue;
        }
        first = false;
        if (len > 0) {
            visitor(line, ctx);
        }
    }

    fclose(f);
    sync_unlock(LOCK_LEVEL_SD);
    return ESP_OK;
}

esp_err_t storage_sd_clear_logs(void)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_SD, 500);
    if (err != ESP_OK) {
        return err;
    }

    static const char *const files[] = { "access.csv", "lighting.csv", "audit.csv" };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        char path[64];
        build_path(files[i], path, sizeof(path));
        unlink(path);
    }

    sync_unlock(LOCK_LEVEL_SD);
    return ESP_OK;
}

esp_err_t storage_sd_get_status(sd_status_t *out_status)
{
    if (out_status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = sync_lock(LOCK_LEVEL_SD, CFG_LOCK_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    out_status->mounted = s_mounted;
    out_status->access_log_bytes = s_mounted ? get_file_size_unlocked("access.csv") : 0;
    out_status->lighting_log_bytes = s_mounted ? get_file_size_unlocked("lighting.csv") : 0;
    out_status->audit_log_bytes = s_mounted ? get_file_size_unlocked("audit.csv") : 0;
    out_status->write_ok_count = s_write_ok;
    out_status->write_err_count = s_write_err;

    sync_unlock(LOCK_LEVEL_SD);
    return ESP_OK;
}
