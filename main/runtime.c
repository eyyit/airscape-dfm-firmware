#include "runtime.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/queue.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

TaskHandle_t sensor_task_handle, metrics_task_handle;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static dfm_state_t current={.error=DFM_NOT_READY};
static dfm_config_t effective;
static dfm_runtime_t status;
static dfm_history_t history[DFM_HISTORY];
static dfm_log_t logs[DFM_LOGS];
static size_t history_count, history_head, log_count, log_head;
static uint64_t log_sequence;
static QueueHandle_t commands;
static StaticQueue_t command_control;
static uint8_t command_storage[DFM_CMD_QUEUE*sizeof(dfm_command_t)];
static bool sensor_reset, filter_reset;
static bool config_pending;
static dfm_config_t proposed;
static dfm_config_t persisted;
static bool have_persisted;

void runtime_log(const char *format, ...) {
    char line[DFM_LOG_LENGTH];
    va_list args; va_start(args,format); vsnprintf(line,sizeof(line),format,args); va_end(args);
    uint64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    dfm_log_t *entry=&logs[log_head];
    entry->sequence=++log_sequence; entry->time_us=now;
    memcpy(entry->message,line,sizeof(line)); entry->message[sizeof(line)-1]=0;
    log_head=(log_head+1)%DFM_LOGS;
    if (log_count<DFM_LOGS) log_count++;
    portEXIT_CRITICAL(&mux);
}
/* Bounded RAM logging; no blocking serial writes on the sensor path. */
static int capture_log(const char *format,va_list args) {
    /* SDK payloads can include credentials or packet data. Never format/copy
       them into the console, support bundle, serial port or RAM log ring. */
    (void)format;(void)args;
    return 0;
}
void runtime_snapshot(dfm_state_t *s,dfm_config_t *c,dfm_runtime_t *r) {
    portENTER_CRITICAL(&mux);
    if(s) *s=current;
    if(c) *c=effective;
    if(r) *r=status;
    portEXIT_CRITICAL(&mux);
}
void runtime_config_read(dfm_config_t *c) { runtime_snapshot(NULL,c,NULL); }
bool runtime_sensor_config_boundary(dfm_config_t *c) {
    portENTER_CRITICAL(&mux);
    bool changed=config_pending;
    if(changed) {
        effective=proposed;status.revision++;current.valid=false;config_pending=false;
    }
    *c=effective;
    portEXIT_CRITICAL(&mux);
    return changed;
}
static bool apply_at_boundary(const dfm_config_t *c) {
    portENTER_CRITICAL(&mux);proposed=*c;config_pending=true;portEXIT_CRITICAL(&mux);
    /* Control worker alone waits; acquisition/metrics never wait for it. */
    for(unsigned attempt=0;attempt<240;attempt++) {
        portENTER_CRITICAL(&mux);bool pending=config_pending;portEXIT_CRITICAL(&mux);
        if(!pending) return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    portENTER_CRITICAL(&mux);bool late_applied=!config_pending;config_pending=false;portEXIT_CRITICAL(&mux);
    return late_applied;
}
void runtime_publish(const dfm_state_t *s) {
    portENTER_CRITICAL(&mux);
    current=*s;
    history[history_head]=(dfm_history_t){s->attempted_us,s->frame.sensor_pa,
        s->corrected_pa,s->frame.temperature_c,s->valid,(int8_t)status.rssi};
    history_head=(history_head+1)%DFM_HISTORY;
    if(history_count<DFM_HISTORY) history_count++;
    portEXIT_CRITICAL(&mux);
}
void runtime_update_network(const dfm_runtime_t *n) {
    portENTER_CRITICAL(&mux);
    status.connected=n->connected; status.power_save_applied=n->power_save_applied;
    status.rssi=n->rssi; status.channel=n->channel; status.disconnect_reason=n->disconnect_reason;
    status.reconnects=n->reconnects; status.connected_us=n->connected_us;
    status.provisioned=n->provisioned;
    status.roam_scans=n->roam_scans;status.roam_attempts=n->roam_attempts;status.roam_scan_errors=n->roam_scan_errors;
    memcpy(status.ip,n->ip,sizeof(status.ip)); memcpy(status.gateway,n->gateway,sizeof(status.gateway));
    memcpy(status.netmask,n->netmask,sizeof(status.netmask)); memcpy(status.dns,n->dns,sizeof(status.dns));
    memcpy(status.bssid,n->bssid,sizeof(status.bssid));
    portEXIT_CRITICAL(&mux);
}
void runtime_management_ready(bool ready) {
    portENTER_CRITICAL(&mux); status.management_ready=ready; portEXIT_CRITICAL(&mux);
}
void runtime_primary_ready(bool ready) {
    portENTER_CRITICAL(&mux); status.metrics_ready=ready; portEXIT_CRITICAL(&mux);
}
void runtime_metric_result(bool rejected,bool error,uint32_t duration) {
    portENTER_CRITICAL(&mux);
    status.metrics_requests++; status.metrics_rejected+=rejected; status.metrics_errors+=error;
    if(duration>status.metrics_max_us) status.metrics_max_us=duration;
    portEXIT_CRITICAL(&mux);
}
void runtime_reject_management(void) {
    portENTER_CRITICAL(&mux); status.management_rejected++; portEXIT_CRITICAL(&mux);
}
void runtime_health(void) {
    uint32_t free=esp_get_free_heap_size(), minimum=esp_get_minimum_free_heap_size();
    uint32_t largest=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    uint32_t ss=sensor_task_handle?uxTaskGetStackHighWaterMark(sensor_task_handle):0;
    uint32_t ms=metrics_task_handle?uxTaskGetStackHighWaterMark(metrics_task_handle):0;
    portENTER_CRITICAL(&mux);
    status.free_heap=free; status.min_heap=minimum; status.largest_heap=largest;
    status.sensor_stack=ss; status.metrics_stack=ms;
    portEXIT_CRITICAL(&mux);
}
size_t runtime_logs(dfm_log_t *out,size_t capacity,uint64_t *dropped) {
    portENTER_CRITICAL(&mux);
    size_t count=log_count<capacity?log_count:capacity;
    for(size_t i=0;i<count;i++) out[i]=logs[(log_head+DFM_LOGS-count+i)%DFM_LOGS];
    *dropped=log_sequence-log_count;
    portEXIT_CRITICAL(&mux); return count;
}
/* Copy at most the consumer's bounded batch, never perform I/O under mux. */
size_t runtime_logs_after(uint64_t cursor,dfm_log_t *out,size_t capacity,uint64_t *lost) {
    portENTER_CRITICAL(&mux);
    uint64_t oldest=log_sequence-log_count+1;
    *lost=cursor<oldest-1?oldest-1-cursor:0;
    uint64_t first=cursor<oldest?oldest:cursor+1;
    size_t count=0;
    while(first<=log_sequence && count<capacity) {
        out[count++]=logs[(log_head+DFM_LOGS-(log_sequence-first+1))%DFM_LOGS];first++;
    }
    portEXIT_CRITICAL(&mux);return count;
}
void runtime_syslog_result(uint64_t sent,uint64_t errors,uint64_t dropped) {
    portENTER_CRITICAL(&mux);
    status.syslog_sent+=sent;status.syslog_errors+=errors;status.syslog_dropped+=dropped;
    portEXIT_CRITICAL(&mux);
}
size_t runtime_history(dfm_history_t *out,size_t capacity) {
    portENTER_CRITICAL(&mux);
    size_t count=history_count<capacity?history_count:capacity;
    for(size_t i=0;i<count;i++) out[i]=history[(history_head+DFM_HISTORY-count+i)%DFM_HISTORY];
    portEXIT_CRITICAL(&mux); return count;
}
bool runtime_queue(dfm_command_t *command) {
    if(!commands || xQueueSend(commands,command,0)!=pdTRUE) {runtime_reject_management();return false;}
    return true;
}
bool runtime_take_sensor_action(bool *filter) {
    portENTER_CRITICAL(&mux);
    bool reset=sensor_reset; *filter=filter_reset;
    sensor_reset=false; filter_reset=false;
    portEXIT_CRITICAL(&mux); return reset;
}
static void worker(void *arg) {
    (void)arg;
    dfm_command_t cmd;
    dfm_config_t previous=effective;
    int64_t trial_until=0, log_until=0;
    for(;;) {
        if(xQueueReceive(commands,&cmd,pdMS_TO_TICKS(250))==pdTRUE) {
            if(cmd.type==CMD_CONFIG) {
                if(!dfm_config_valid(&cmd.config)) {runtime_log("config rejected: invalid");continue;}
                dfm_config_t before; dfm_runtime_t r; runtime_snapshot(NULL,&before,&r);
                if(cmd.revision!=r.revision || trial_until) {runtime_log("config rejected: revision/trial");continue;}
                bool radio_changed=cmd.config.power_save!=before.power_save;
                bool changed=!dfm_config_equal(&cmd.config,&before);
                if(radio_changed && cmd.persist) {runtime_log("config rejected: radio trial must be temporary");continue;}
                if(radio_changed && network_power_save(cmd.config.power_save)!=ESP_OK) {
                    runtime_log("power-save apply/readback failed");continue;
                }
                previous=before;
                if(changed && !apply_at_boundary(&cmd.config)) {
                    if(radio_changed) network_power_save(before.power_save);
                    runtime_log("configuration not applied: sensor boundary timed out");continue;
                }
                if(!cmd.persist && changed) trial_until=esp_timer_get_time()+120000000;
                if(cmd.persist) {
                    if(have_persisted && dfm_config_equal(&persisted,&cmd.config)) {
                        runtime_log("saved configuration unchanged; no NVS write");continue;
                    }
                    nvs_handle_t nvs;
                    esp_err_t err=nvs_open_from_partition("dfm_data","dfm_custom",NVS_READWRITE,&nvs);
                    if(err==ESP_OK) {
                        err=nvs_set_blob(nvs,"config",&cmd.config,sizeof(cmd.config));
                        if(err==ESP_OK) err=nvs_commit(nvs);
                        nvs_close(nvs);
                    }
                    if(err==ESP_OK) {persisted=cmd.config;have_persisted=true;}
                    runtime_log("configuration persistence %s",esp_err_to_name(err));
                } else runtime_log("temporary config revision %lu applied",(unsigned long)(r.revision+(changed?1:0)));
            } else if(cmd.type==CMD_CONFIRM_TRIAL) {
                trial_until=0; runtime_log("radio trial confirmed for this boot; save separately if desired");
            } else if(cmd.type==CMD_SENSOR_RESET || cmd.type==CMD_FILTER_RESET) {
                portENTER_CRITICAL(&mux);
                if(cmd.type==CMD_SENSOR_RESET) sensor_reset=true; else filter_reset=true;
                portEXIT_CRITICAL(&mux); runtime_log("sensor action queued at sample boundary");
            } else if(cmd.type==CMD_WIFI_RECONNECT) {network_reconnect();runtime_log("Wi-Fi reconnect requested");}
            else if(cmd.type==CMD_REBOOT) {runtime_log("explicit reboot");vTaskDelay(pdMS_TO_TICKS(500));esp_restart();}
            else if(cmd.type==CMD_LOG_LEVEL) {
                esp_log_level_set("*",cmd.value);log_until=esp_timer_get_time()+120000000;
                runtime_log("log level %d expires in 120 seconds",cmd.value);
            }
        }
        if(trial_until && esp_timer_get_time()>=trial_until) {
            if(network_power_save(previous.power_save)==ESP_OK) {
                if(apply_at_boundary(&previous)) {
                    trial_until=0;runtime_log("unconfirmed temporary settings reverted");
                }
            }
        }
        if(log_until && esp_timer_get_time()>=log_until) {
            esp_log_level_set("*",ESP_LOG_INFO);log_until=0;runtime_log("debug logging expired");
        }
        runtime_health();
    }
}
void runtime_init(void) {
    effective=dfm_defaults();
    nvs_handle_t nvs;
    if(nvs_open_from_partition("dfm_data","dfm_custom",NVS_READONLY,&nvs)==ESP_OK) {
        dfm_config_t saved; size_t length=sizeof(saved);
        if(nvs_get_blob(nvs,"config",&saved,&length)==ESP_OK && length==sizeof(saved) && dfm_config_valid(&saved)) {
            effective=saved;persisted=saved;have_persisted=true;
        }
        nvs_close(nvs);
    }
    status.rssi=-127; status.revision=1;status.boot_id=esp_random();status.reset_reason=esp_reset_reason();
    esp_log_set_vprintf(capture_log);
    commands=xQueueCreateStatic(DFM_CMD_QUEUE,sizeof(dfm_command_t),command_storage,&command_control);
    configASSERT(commands);
    configASSERT(xTaskCreate(worker,"dfm_control",6144,NULL,3,NULL)==pdPASS);
    runtime_log("boot: management lower priority; sensor=10 metrics=8 control=3 HTTP=2 syslog=1");
}
