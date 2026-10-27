#include "runtime.h"
#include "device_identity.h"
#include "ota.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Management owns all JSON allocation and HTTP controls. Its memory and concurrency
   are bounded; refusing diagnostics is preferable to consuming sensor reserves. */
static dfm_history_t history_copy[DFM_HISTORY];
static dfm_log_t log_copy[DFM_LOGS];
static char json_output[32768];
static bool json_allocation_failed;
static void *diagnostic_alloc(size_t size) {
    if(size>32768 || esp_get_free_heap_size()<65536+size) {
        json_allocation_failed=true;return NULL;
    }
    void *memory=malloc(size);
    if(!memory) json_allocation_failed=true;
    return memory;
}
static bool admit(httpd_req_t *request) {
    if(esp_get_free_heap_size()<49152) {
        runtime_reject_management();httpd_resp_set_status(request,"503 Service Unavailable");
        httpd_resp_sendstr(request,"Diagnostics deferred: reserved sensor capacity");return false;
    }
    return true;
}
static esp_err_t send_json(httpd_req_t *request,cJSON *object) {
    httpd_resp_set_type(request,"application/json");
    httpd_resp_set_hdr(request,"Cache-Control","no-store");
    bool ok=object && !json_allocation_failed && cJSON_PrintPreallocated(object,json_output,sizeof(json_output),false);
    cJSON_Delete(object);
    if(!ok) {httpd_resp_set_status(request,"503 Service Unavailable");return httpd_resp_sendstr(request,"{\"error\":\"diagnostic buffer exhausted\"}");}
    return httpd_resp_sendstr(request,json_output);
}
static void number(cJSON *o,const char *name,double value) {cJSON_AddNumberToObject(o,name,value);}
static cJSON *configuration(const dfm_config_t *c) {
    cJSON *o=cJSON_CreateObject();
    number(o,"schema",c->schema);number(o,"interval_ms",c->interval_ms);number(o,"timeout_ms",c->timeout_ms);
    number(o,"recovery_failures",c->recovery_failures);number(o,"backoff_ms",c->backoff_ms);
    number(o,"stale_ms",c->stale_ms);number(o,"alpha",c->alpha);number(o,"offset_pa",c->offset_pa);
    number(o,"orientation",c->orientation);number(o,"setpoint_pa",c->setpoint_pa);number(o,"hysteresis_pa",c->hysteresis_pa);
    cJSON_AddBoolToObject(o,"power_save",c->power_save);return o;
}
static cJSON *status_json(void) {
    dfm_state_t s;dfm_config_t c;dfm_runtime_t r;runtime_snapshot(&s,&c,&r);
    int64_t now=esp_timer_get_time();bool fresh=dfm_fresh(&s,&c,now);
    cJSON *o=cJSON_CreateObject();
    cJSON_AddStringToObject(o,"firmware",DFM_VERSION);cJSON_AddStringToObject(o,"sdk",esp_get_idf_version());
    cJSON_AddStringToObject(o,"stage","development; hardware acceptance pending");
    number(o,"boot_id",r.boot_id);number(o,"uptime_seconds",now/1e6);number(o,"reset_reason",r.reset_reason);
    number(o,"revision",r.revision);cJSON_AddBoolToObject(o,"valid",fresh);
    if(s.have_sample) number(o,"sample_age_seconds",(now-s.acquired_us)/1e6);
    number(o,"error_code",s.error);number(o,"consecutive_errors",s.consecutive_errors);
    number(o,"crc_valid_mask",s.crc_valid_mask);
    char hex[28];for(unsigned i=0;i<9;i++) snprintf(hex+3*i,4,"%02x ",s.last_frame[i]);
    cJSON_AddStringToObject(o,"last_frame_hex",hex);
    cJSON *frame=cJSON_AddObjectToObject(o,"last_good");
    if(s.have_sample) {
        number(frame,"counts",s.frame.counts);number(frame,"temperature_counts",s.frame.temperature_counts);number(frame,"scale",s.frame.scale);
        number(frame,"sensor_pa",s.frame.sensor_pa);number(frame,"filter_pa",s.filter_pa);
        number(frame,"raw_dp_pa",s.raw_pa);number(frame,"canonical_dp_pa",s.canonical_pa);
        number(frame,"cor_dp_pa",s.corrected_pa);number(frame,"temp_c",s.frame.temperature_c);
    }
    /* Flat current-sample fields for simple pollers, alongside full diagnostics.
       Missing/invalid current samples are null; last_good stays diagnostic. */
    const char *pressure_fields[]={"sensor_dp_pa","raw_dp_pa","canonical_dp_pa","cor_dp_pa","processing_difference_pa","temp_c","temp_f"};
    double pressure_values[]={s.frame.sensor_pa,s.raw_pa,s.canonical_pa,s.corrected_pa,s.corrected_pa-s.frame.sensor_pa,s.frame.temperature_c,s.frame.temperature_c*1.8+32};
    for(unsigned i=0;i<7;i++) {
        if(fresh) number(o,pressure_fields[i],pressure_values[i]);else cJSON_AddNullToObject(o,pressure_fields[i]);
    }
    number(o,"adj_dp_pa",c.offset_pa);number(o,"setpoint_pa",c.setpoint_pa);number(o,"hysteresis_pa",c.hysteresis_pa);
    number(o,"wifi_rssi",r.rssi);cJSON_AddBoolToObject(o,"temp_valid",fresh);
    cJSON_AddBoolToObject(o,"high_static",fresh && s.high_static);
    number(o,"successful_samples",s.successes);
    cJSON *sensor=cJSON_AddObjectToObject(o,"sensor");
    cJSON_AddStringToObject(sensor,"model","Sensirion SDP810-500Pa");
    cJSON_AddStringToObject(sensor,"measurement","one differential pressure; separate port pressures are not available");
    number(sensor,"i2c_address",0x25);number(sensor,"sda_gpio",22);number(sensor,"scl_gpio",23);number(sensor,"i2c_hz",100000);
    cJSON_AddStringToObject(sensor,"difference_definition","corrected differential minus unsmoothed sensor differential");
    number(o,"i2c_errors",s.errors[0]);number(o,"crc_errors",s.errors[1]);number(o,"scale_errors",s.errors[2]);
    number(o,"recoveries",s.recoveries);number(o,"missed_deadlines",s.missed_deadlines);
    number(o,"sample_duration_us",s.duration_us);number(o,"sample_max_us",s.max_duration_us);
    cJSON_AddItemToObject(o,"config",configuration(&c));
    cJSON *net=cJSON_AddObjectToObject(o,"network");
    cJSON_AddBoolToObject(net,"connected",r.connected);cJSON_AddBoolToObject(net,"provisioned",r.provisioned);
    cJSON_AddBoolToObject(net,"power_save_actual",r.power_save_applied);
    cJSON_AddStringToObject(net,"ip",r.ip);cJSON_AddStringToObject(net,"gateway",r.gateway);
    cJSON_AddStringToObject(net,"netmask",r.netmask);cJSON_AddStringToObject(net,"bssid",r.bssid);
    number(net,"rssi",r.rssi);number(net,"channel",r.channel);number(net,"disconnect_reason",r.disconnect_reason);
    number(net,"reconnects",r.reconnects);number(net,"connected_seconds",r.connected?(now-r.connected_us)/1e6:0);
    cJSON_AddBoolToObject(net,"roaming_enabled",true);
    number(net,"roam_scans",r.roam_scans);number(net,"roam_attempts",r.roam_attempts);
    number(net,"roam_scan_errors",r.roam_scan_errors);
    number(net,"roam_weak_dbm",-75);number(net,"roam_min_candidate_dbm",-78);
    number(net,"roam_gain_db",12);number(net,"roam_weak_seconds",60);number(net,"roam_cooldown_seconds",300);
    cJSON *sys=cJSON_AddObjectToObject(o,"system");
    number(sys,"free_heap",r.free_heap);number(sys,"minimum_heap",r.min_heap);number(sys,"largest_block",r.largest_heap);
    number(sys,"sensor_stack_free",r.sensor_stack);number(sys,"metrics_stack_free",r.metrics_stack);
    number(sys,"metrics_requests",r.metrics_requests);number(sys,"metrics_rejected",r.metrics_rejected);
    number(sys,"metrics_errors",r.metrics_errors);number(sys,"metrics_max_us",r.metrics_max_us);
    number(sys,"management_rejected",r.management_rejected);
    cJSON_AddBoolToObject(sys,"metrics_ready",r.metrics_ready);
    cJSON_AddBoolToObject(sys,"management_ready",r.management_ready);
    dfm_ota_status_t update;dfm_ota_status(&update);
    cJSON_AddBoolToObject(sys,"ota_upload_enabled",update.enabled);
    cJSON *logging=cJSON_AddObjectToObject(o,"syslog");
    cJSON_AddStringToObject(logging,"destination",DFM_SYSLOG_ADDRESS[0] ? DFM_SYSLOG_ADDRESS ":" DFM_STRINGIFY(DFM_SYSLOG_PORT) "/UDP" : "disabled");
    cJSON_AddStringToObject(logging,"hostname",DFM_DEVICE_HOSTNAME);
    number(logging,"sent",r.syslog_sent);number(logging,"errors",r.syslog_errors);number(logging,"dropped",r.syslog_dropped);
    cJSON_AddStringToObject(logging,"delivery","best effort; sent is not delivery confirmation");
    cJSON *ota=cJSON_AddObjectToObject(o,"ota");
    cJSON_AddBoolToObject(ota,"enabled",update.enabled);
    cJSON_AddBoolToObject(ota,"busy",update.busy);
    cJSON_AddBoolToObject(ota,"staged",update.staged);
    cJSON_AddBoolToObject(ota,"selected",update.selected);
    cJSON_AddBoolToObject(ota,"pending_verify",update.pending_verify);
    number(ota,"received",update.received);number(ota,"total",update.total);
    cJSON_AddStringToObject(ota,"error",update.error);
    cJSON_AddStringToObject(ota,"running_elf_sha256",update.running_elf_sha256);
    cJSON_AddStringToObject(ota,"staged_sha256",update.staged_sha256);
    return o;
}
static cJSON *logs_json(void) {
    uint64_t dropped;size_t count=runtime_logs(log_copy,DFM_LOGS,&dropped);
    cJSON *o=cJSON_CreateObject();number(o,"overwritten",dropped);
    cJSON *entries=cJSON_AddArrayToObject(o,"entries");
    for(size_t i=0;i<count;i++) {
        cJSON *row=cJSON_CreateObject();number(row,"sequence",log_copy[i].sequence);
        number(row,"seconds",log_copy[i].time_us/1e6);cJSON_AddStringToObject(row,"message",log_copy[i].message);
        cJSON_AddItemToArray(entries,row);
    }
    return o;
}
static cJSON *history_json(void) {
    size_t count=runtime_history(history_copy,DFM_HISTORY);cJSON *array=cJSON_CreateArray();
    for(size_t i=0;i<count;i++) {
        dfm_history_t *s=&history_copy[i];cJSON *row=cJSON_CreateObject();
        number(row,"seconds",s->time_us/1e6);cJSON_AddBoolToObject(row,"valid",s->valid);
        if(s->valid) {number(row,"pressure_pa",s->corrected_pa);number(row,"temp_c",s->temp_c);}
        cJSON_AddItemToArray(array,row);
    }
    return array;
}
static esp_err_t get_api(httpd_req_t *request) {
    if(!admit(request)) return ESP_OK;
    json_allocation_failed=false;
    if(strcmp(request->uri,"/api/v1/status")==0) return send_json(request,status_json());
    if(strcmp(request->uri,"/api/v1/logs")==0) return send_json(request,logs_json());
    if(strcmp(request->uri,"/api/v1/history")==0) return send_json(request,history_json());
    cJSON *bundle=status_json();cJSON_AddItemToObject(bundle,"logs",logs_json());
    cJSON_AddItemToObject(bundle,"history",history_json());
    httpd_resp_set_hdr(request,"Content-Disposition","attachment; filename=dfm-support.json");
    return send_json(request,bundle);
}
static cJSON *read_body(httpd_req_t *request) {
    if(request->content_len<=0 || request->content_len>1024) return NULL;
    char body[1025];int received=0;
    while(received<request->content_len) {
        int n=httpd_req_recv(request,body+received,request->content_len-received);
        if(n<=0) return NULL;
        received+=n;
    }
    body[received]=0;
    const char *end=NULL;cJSON *o=cJSON_ParseWithOpts(body,&end,true);
    if(!cJSON_IsObject(o)) {cJSON_Delete(o);return NULL;}
    return o;
}
static bool known_key(const char *key) {
    const char *keys[]={"revision","persist","interval_ms","timeout_ms","recovery_failures","backoff_ms",
        "stale_ms","alpha","offset_pa","orientation","setpoint_pa","hysteresis_pa","power_save"};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++) if(strcmp(key,keys[i])==0) return true;
    return false;
}
static bool parse_config(cJSON *body,dfm_command_t *cmd) {
    cJSON *entry; cJSON_ArrayForEach(entry,body) {
        if(!entry->string || !known_key(entry->string)) return false;
        for(cJSON *other=entry->next;other;other=other->next)
            if(other->string && strcmp(entry->string,other->string)==0) return false;
        if(strcmp(entry->string,"persist")==0 || strcmp(entry->string,"power_save")==0) {
            if(!cJSON_IsBool(entry)) return false;
        } else if(!cJSON_IsNumber(entry) || !isfinite(entry->valuedouble)) return false;
    }
    cJSON *rev=cJSON_GetObjectItemCaseSensitive(body,"revision");
    cJSON *persist=cJSON_GetObjectItemCaseSensitive(body,"persist");
    if(!cJSON_IsNumber(rev) || rev->valuedouble<1 || rev->valuedouble>UINT32_MAX || floor(rev->valuedouble)!=rev->valuedouble || !cJSON_IsBool(persist)) return false;
    cmd->revision=(uint32_t)rev->valuedouble;cmd->persist=cJSON_IsTrue(persist);cmd->type=CMD_CONFIG;
    runtime_config_read(&cmd->config);
#define U32_FIELD(name) do { cJSON *v=cJSON_GetObjectItemCaseSensitive(body,#name); if(v) { if(v->valuedouble<0 || v->valuedouble>60000 || floor(v->valuedouble)!=v->valuedouble) return false; cmd->config.name=(uint32_t)v->valuedouble; } } while(0)
#define FLOAT_FIELD(name) do {cJSON *v=cJSON_GetObjectItemCaseSensitive(body,#name); if(v) cmd->config.name=(float)v->valuedouble;} while(0)
    U32_FIELD(interval_ms);U32_FIELD(timeout_ms);U32_FIELD(recovery_failures);U32_FIELD(backoff_ms);U32_FIELD(stale_ms);
    FLOAT_FIELD(alpha);FLOAT_FIELD(offset_pa);FLOAT_FIELD(setpoint_pa);FLOAT_FIELD(hysteresis_pa);
    cJSON *orientation=cJSON_GetObjectItemCaseSensitive(body,"orientation");
    if(orientation) {
        if(orientation->valuedouble!=1 && orientation->valuedouble!=-1) return false;
        cmd->config.orientation=(int32_t)orientation->valuedouble;
    }
    cJSON *ps=cJSON_GetObjectItemCaseSensitive(body,"power_save");if(ps) cmd->config.power_save=cJSON_IsTrue(ps);
    if(cmd->persist && (cmd->config.interval_ms!=1000 || fabsf(cmd->config.alpha-0.2f)>0.000001f)) return false;
    return dfm_config_valid(&cmd->config);
}
static esp_err_t ota_api(httpd_req_t *request) {
    if(!admit(request)) return ESP_OK;
    json_allocation_failed=false;
    if(!strcmp(request->uri,"/api/v1/ota")) return dfm_ota_upload(request);
    return dfm_ota_activate(request);
}
static esp_err_t post_api(httpd_req_t *request) {
    if(!admit(request)) return ESP_OK;
    json_allocation_failed=false;
    /* No authentication by owner request. JSON-only writes, no CORS/OPTIONS,
       and explicit action acknowledgment prevent accidental cross-site forms. */
    char type[40];
    if(httpd_req_get_hdr_value_str(request,"Content-Type",type,sizeof(type))!=ESP_OK || strcmp(type,"application/json")) {
        httpd_resp_set_status(request,"415 Unsupported Media Type");return httpd_resp_sendstr(request,"JSON request required");
    }
    cJSON *body=read_body(request);dfm_command_t command={0};bool valid=body!=NULL;
    if(valid && strcmp(request->uri,"/api/v1/config")==0) valid=parse_config(body,&command);
    else if(valid) {
        cJSON *action=cJSON_GetObjectItemCaseSensitive(body,"action");
        cJSON *ack=cJSON_GetObjectItemCaseSensitive(body,"acknowledge_interruption");
        valid=cJSON_IsString(action) && cJSON_IsTrue(ack);
        if(valid) {
            if(strcmp(action->valuestring,"sensor_reset")==0) command.type=CMD_SENSOR_RESET;
            else if(strcmp(action->valuestring,"filter_reset")==0) command.type=CMD_FILTER_RESET;
            else if(strcmp(action->valuestring,"wifi_reconnect")==0) command.type=CMD_WIFI_RECONNECT;
            else if(strcmp(action->valuestring,"reboot")==0) command.type=CMD_REBOOT;
            else if(strcmp(action->valuestring,"confirm_trial")==0) command.type=CMD_CONFIRM_TRIAL;
            else valid=false;
        }
    }
    cJSON_Delete(body);
    if(json_allocation_failed) {
        runtime_reject_management();httpd_resp_set_status(request,"503 Service Unavailable");
        return httpd_resp_sendstr(request,"{\"error\":\"diagnostics memory budget exhausted\"}");
    }
    if(!valid) {httpd_resp_set_status(request,"400 Bad Request");return httpd_resp_sendstr(request,"{\"error\":\"invalid request or setting range\"}");}
    if(!runtime_queue(&command)) {httpd_resp_set_status(request,"503 Service Unavailable");return httpd_resp_sendstr(request,"{\"error\":\"control queue full\"}");}
    httpd_resp_set_status(request,"202 Accepted");httpd_resp_set_type(request,"application/json");
    return httpd_resp_sendstr(request,"{\"queued\":true,\"applied\":false,\"check\":\"status and event log\"}");
}
extern const uint8_t index_start[] __asm__("_binary_index_html_start");
extern const uint8_t index_end[] __asm__("_binary_index_html_end");
extern const uint8_t script_start[] __asm__("_binary_console_js_start");
extern const uint8_t script_end[] __asm__("_binary_console_js_end");
static esp_err_t console_script(httpd_req_t *request) {
    httpd_resp_set_type(request,"application/javascript; charset=utf-8");
    httpd_resp_set_hdr(request,"X-Content-Type-Options","nosniff");
    return httpd_resp_send(request,(const char *)script_start,script_end-script_start);
}
static esp_err_t console_page(httpd_req_t *request) {
    httpd_resp_set_type(request,"text/html; charset=utf-8");
    httpd_resp_set_hdr(request,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(request,"Content-Security-Policy","default-src 'self'; script-src 'self'; style-src 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'");
    return httpd_resp_send(request,(const char *)index_start,index_end-index_start);
}
void management_start(void) {
    cJSON_Hooks hooks={.malloc_fn=diagnostic_alloc,.free_fn=free};
    cJSON_InitHooks(&hooks);
    httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();
    cfg.server_port=8080;cfg.task_priority=2;cfg.stack_size=8192;cfg.max_open_sockets=2;
    cfg.max_uri_handlers=12;cfg.ctrl_port=32769;
    cfg.recv_wait_timeout=1;cfg.send_wait_timeout=1;cfg.lru_purge_enable=true;
    httpd_handle_t server=NULL;esp_err_t result=httpd_start(&server,&cfg);
    if(result!=ESP_OK) {runtime_log("HTTP diagnostics unavailable: %s",esp_err_to_name(result));return;}
    const httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=console_page},
        {.uri="/console.js",.method=HTTP_GET,.handler=console_script},
        {.uri="/api/v1/status",.method=HTTP_GET,.handler=get_api},
        {.uri="/api/v1/logs",.method=HTTP_GET,.handler=get_api},
        {.uri="/api/v1/history",.method=HTTP_GET,.handler=get_api},
        {.uri="/api/v1/support",.method=HTTP_GET,.handler=get_api},
        {.uri="/api/v1/config",.method=HTTP_POST,.handler=post_api},
        {.uri="/api/v1/action",.method=HTTP_POST,.handler=post_api},
        {.uri="/api/v1/ota",.method=HTTP_POST,.handler=ota_api},
        {.uri="/api/v1/ota/activate",.method=HTTP_POST,.handler=ota_api},
    };
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++) ESP_ERROR_CHECK(httpd_register_uri_handler(server,&routes[i]));
    runtime_management_ready(true);runtime_log("plain HTTP diagnostics ready on 8080; no login; signed OTA retained");
}
