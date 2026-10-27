#pragma once
#include "esp_http_server.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool enabled, busy, staged, selected, pending_verify;
    uint32_t received, total;
    char error[80], staged_sha256[65], running_elf_sha256[65], version[32];
} dfm_ota_status_t;
void dfm_ota_status(dfm_ota_status_t *status);
esp_err_t dfm_ota_upload(httpd_req_t *request);
esp_err_t dfm_ota_activate(httpd_req_t *request);
void dfm_ota_health_start(void);
