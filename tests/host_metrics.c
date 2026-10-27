/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "host_runtime.h"
#include <stdio.h>
#include <signal.h>
TaskHandle_t metrics_task_handle;
void runtime_snapshot(dfm_state_t *state,dfm_config_t *config,dfm_runtime_t *runtime) {
    *config=dfm_defaults();
    *state=(dfm_state_t){.valid=true,.have_sample=true,.acquired_us=(uint64_t)esp_timer_get_time()-100000,
        .corrected_pa=0,.frame={.temperature_c=27}};
    *runtime=(dfm_runtime_t){.connected=true,.rssi=-55,.free_heap=100000,.revision=1};
}
void runtime_log(const char *format,...) {(void)format;}
void runtime_metric_result(bool rejected,bool error,uint32_t us) {(void)rejected;(void)error;(void)us;}
void runtime_primary_ready(bool ready) {(void)ready;}
static volatile sig_atomic_t stopping;
static void stop(int signal_number){(void)signal_number;stopping=1;}
int main(void) {
 signal(SIGTERM,stop);metrics_start();
 while(!stopping)usleep(10000);
 pthread_cancel(metrics_task_handle);pthread_join(metrics_task_handle,NULL);return 0;
}
