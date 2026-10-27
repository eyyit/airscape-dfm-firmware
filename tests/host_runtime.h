/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
/* POSIX test adapter. Production metrics.c is compiled unchanged apart from
   headers/port. Its socket state machine and formatter are the firmware code. */
#pragma once
#include "dfm_core.h"
#include "dfm_version.h"
#include <assert.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef pthread_t TaskHandle_t;
typedef struct {uint64_t syslog_sent,syslog_errors,syslog_dropped;
    uint64_t roam_scans,roam_attempts,roam_scan_errors;
    bool connected,power_save_applied;int rssi;
    uint64_t reconnects;uint32_t free_heap,revision;
} dfm_runtime_t;
extern TaskHandle_t metrics_task_handle;
#define configASSERT assert
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
static inline int64_t esp_timer_get_time(void) {
    struct timespec time;clock_gettime(CLOCK_MONOTONIC,&time);
    return (int64_t)time.tv_sec*1000000+time.tv_nsec/1000;
}
static inline void vTaskDelay(unsigned ms) {usleep(ms*1000);}
static inline void vTaskDelete(void *arg) {(void)arg;pthread_exit(NULL);}
typedef struct { void (*function)(void *);void *arg; } host_task_t;
static inline void *host_thread(void *data) {
    host_task_t copy=*(host_task_t *)data;free(data);copy.function(copy.arg);return NULL;
}
static inline int xTaskCreate(void (*function)(void *),const char *name,unsigned stack,
                             void *arg,unsigned priority,TaskHandle_t *handle) {
    (void)name;(void)stack;(void)priority;
    host_task_t *task=malloc(sizeof(*task));assert(task);*task=(host_task_t){function,arg};
    return pthread_create(handle,NULL,host_thread,task)==0?pdPASS:0;
}
void runtime_snapshot(dfm_state_t *,dfm_config_t *,dfm_runtime_t *);
void runtime_log(const char *,...);
void runtime_metric_result(bool,bool,uint32_t);
void runtime_primary_ready(bool);
void metrics_start(void);
