/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include "dfm_core.h"
#include "dfm_version.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdarg.h>

#define DFM_HISTORY 180
#define DFM_LOGS 64
#define DFM_LOG_LENGTH 160
#define DFM_CMD_QUEUE 4
typedef struct {
    bool connected, power_save_applied, antenna_external, provisioned, management_ready, metrics_ready;
    int rssi, channel, disconnect_reason;
    uint64_t reconnects, connected_us;
    uint64_t roam_scans, roam_attempts, roam_scan_errors;
    char ip[16], gateway[16], netmask[16], dns[40], bssid[18];
    uint32_t revision;
    uint64_t metrics_requests, metrics_rejected, metrics_errors, metrics_max_us;
    uint64_t management_rejected;
    uint64_t syslog_sent, syslog_errors, syslog_dropped;
    uint32_t free_heap, min_heap, largest_heap;
    uint32_t sensor_stack, metrics_stack;
    uint32_t boot_id, reset_reason;
} dfm_runtime_t;
typedef struct {
    uint64_t time_us; float sensor_pa, corrected_pa, temp_c; bool valid; int8_t rssi;
} dfm_history_t;
typedef struct { uint64_t sequence, time_us; char message[DFM_LOG_LENGTH]; } dfm_log_t;
typedef enum { CMD_CONFIG, CMD_SENSOR_RESET, CMD_FILTER_RESET, CMD_WIFI_RECONNECT,
               CMD_REBOOT, CMD_CONFIRM_TRIAL, CMD_LOG_LEVEL } dfm_command_type_t;
typedef struct {
    dfm_command_type_t type;
    dfm_config_t config;
    uint32_t revision;
    bool persist;
    int value;
} dfm_command_t;
extern TaskHandle_t sensor_task_handle, metrics_task_handle;
void runtime_init(void);
void runtime_snapshot(dfm_state_t *state, dfm_config_t *config, dfm_runtime_t *runtime);
void runtime_publish(const dfm_state_t *state);
void runtime_update_network(const dfm_runtime_t *network);
void runtime_management_ready(bool ready);
void runtime_primary_ready(bool ready);
void runtime_health(void);
void runtime_metric_result(bool rejected, bool error, uint32_t duration_us);
void runtime_reject_management(void);
bool runtime_queue(dfm_command_t *command);
void runtime_config_read(dfm_config_t *config);
bool runtime_sensor_config_boundary(dfm_config_t *config);
bool runtime_take_sensor_action(bool *reset_filter);
void runtime_log(const char *format, ...) __attribute__((format(printf,1,2)));
size_t runtime_logs(dfm_log_t *logs, size_t count, uint64_t *dropped);
size_t runtime_history(dfm_history_t *samples, size_t count);
void sensor_start(void);
void network_start(void);
esp_err_t network_power_save(bool enabled);
void network_reconnect(void);
void metrics_start(void);
void management_start(void);

size_t runtime_logs_after(uint64_t cursor,dfm_log_t *out,size_t capacity,uint64_t *lost);
void runtime_syslog_result(uint64_t sent,uint64_t errors,uint64_t dropped);
void syslog_start(void);
