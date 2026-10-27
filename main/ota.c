/* Independent signed application-only OTA. Never writes bootloader/NVS/eFuses. */
#include "ota.h"
#include "runtime.h"
#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "mbedtls/pk.h"
#include "mbedtls/sha256.h"
#include "mbedtls/constant_time.h"
#include "nvs.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MANIFEST_MAX 1024
#define SIGNATURE_MAX 512
#define PACKAGE_OVERHEAD (16+MANIFEST_MAX+SIGNATURE_MAX)
static dfm_ota_status_t status;
static const esp_partition_t *staged_partition;
static portMUX_TYPE ota_mux=portMUX_INITIALIZER_UNLOCKED;
_Alignas(8) static uint8_t io_buffer[4096];
static void hex(const uint8_t bytes[32],char output[65]) {
    for(unsigned i=0;i<32;i++) snprintf(output+2*i,3,"%02x",bytes[i]);
}
static bool trusted_key(mbedtls_pk_context *key) {
    nvs_handle_t nvs;size_t size=0;
    if(nvs_open_from_partition("dfm_data","dfm_provision",NVS_READONLY,&nvs)!=ESP_OK) return false;
    if(nvs_get_str(nvs,"ota_pubkey",NULL,&size)!=ESP_OK || size<32 || size>2048) {
        nvs_close(nvs);return false;
    }
    char *pem=malloc(size);
    bool ok=pem && nvs_get_str(nvs,"ota_pubkey",pem,&size)==ESP_OK &&
        mbedtls_pk_parse_public_key(key,(const uint8_t *)pem,size)==0 &&
        mbedtls_pk_can_do(key,MBEDTLS_PK_RSA) && mbedtls_pk_get_bitlen(key)==3072;
    free(pem);nvs_close(nvs);return ok;
}
void dfm_ota_status(dfm_ota_status_t *out) {
    portENTER_CRITICAL(&ota_mux);*out=status;portEXIT_CRITICAL(&ota_mux);
    mbedtls_pk_context key;mbedtls_pk_init(&key);out->enabled=trusted_key(&key);mbedtls_pk_free(&key);
    hex(esp_app_get_description()->app_elf_sha256,out->running_elf_sha256);
    esp_ota_img_states_t state;
    const esp_partition_t *running=esp_ota_get_running_partition();
    out->pending_verify=running && esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY;
}
static esp_err_t failure(httpd_req_t *r,const char *message) {
    portENTER_CRITICAL(&ota_mux);status.busy=false;if(!status.selected)status.staged=false;
    snprintf(status.error,sizeof(status.error),"%s",message);portEXIT_CRITICAL(&ota_mux);
    if(!status.selected)staged_partition=NULL;
    runtime_log("OTA rejected: %s",message);
    httpd_resp_set_type(r,"application/json");httpd_resp_set_status(r,"400 Bad Request");
    return httpd_resp_sendstr(r,"{\"staged\":false,\"error\":\"update rejected; see OTA status\"}");
}
static bool maintenance_ack(httpd_req_t *r) {
    char value[8];
    return httpd_req_get_hdr_value_str(r,"X-DFM-Maintenance",value,sizeof(value))==ESP_OK && !strcmp(value,"accept");
}
static bool read_exact(httpd_req_t *r,uint8_t *buffer,size_t length,int64_t deadline) {
    size_t used=0;
    while(used<length) {
        if(esp_timer_get_time()>deadline) return false;
        int n=httpd_req_recv(r,(char *)buffer+used,length-used);
        if(n<=0) return false;
        used+=(size_t)n;
    }
    return true;
}
static uint32_t big32(const uint8_t *p) {return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];}
static bool json_string(cJSON *m,const char *name,const char *expected) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(m,name);
    return cJSON_IsString(v) && !strcmp(v->valuestring,expected);
}
static bool manifest_valid(cJSON *m,size_t *size,char hash[65],char elf[65],char version[32]) {
    if(!cJSON_IsObject(m)) return false;
    cJSON *e;cJSON_ArrayForEach(e,m) for(cJSON *p=e->next;p;p=p->next)
        if(e->string && p->string && !strcmp(e->string,p->string)) return false;
    if(!json_string(m,"board","xiao-esp32c6") || !json_string(m,"chip","esp32c6") ||
       !json_string(m,"algorithm","RSA3072-PKCS1v15-SHA256")) return false;
    cJSON *schema=cJSON_GetObjectItemCaseSensitive(m,"schema");
    cJSON *config=cJSON_GetObjectItemCaseSensitive(m,"config_schema");
    cJSON *length=cJSON_GetObjectItemCaseSensitive(m,"size");
    cJSON *sha=cJSON_GetObjectItemCaseSensitive(m,"sha256");
    cJSON *elfsha=cJSON_GetObjectItemCaseSensitive(m,"elf_sha256");
    cJSON *ver=cJSON_GetObjectItemCaseSensitive(m,"version");
    if(!cJSON_IsNumber(schema) || schema->valuedouble!=1 || !cJSON_IsNumber(config) || config->valuedouble!=1 ||
       !cJSON_IsNumber(length) || !isfinite(length->valuedouble) || length->valuedouble<288 ||
       length->valuedouble>0x1c0000 || floor(length->valuedouble)!=length->valuedouble ||
       !cJSON_IsString(sha) || strlen(sha->valuestring)!=64 || !cJSON_IsString(elfsha) || strlen(elfsha->valuestring)!=64 ||
       !cJSON_IsString(ver) || !ver->valuestring[0] || strlen(ver->valuestring)>=32) return false;
    for(unsigned i=0;i<64;i++) {
        if(!strchr("0123456789abcdef",sha->valuestring[i]) || !strchr("0123456789abcdef",elfsha->valuestring[i])) return false;
    }
    *size=(size_t)length->valuedouble;strcpy(hash,sha->valuestring);strcpy(elf,elfsha->valuestring);strcpy(version,ver->valuestring);
    return true;
}
static bool image_identity(const uint8_t *image,const char *version,const char *elf) {
    const esp_image_header_t *header=(const esp_image_header_t *)image;
    /* ESP app descriptor follows 24-byte image header and 8-byte segment header. */
    const esp_app_desc_t *app=(const esp_app_desc_t *)(image+32);
    char actual[65];hex(app->app_elf_sha256,actual);
    return header->magic==0xe9 && header->chip_id==ESP_CHIP_ID_ESP32C6 && app->magic_word==0xabcd5432 &&
        !strncmp(app->project_name,"airscape_dfm",sizeof(app->project_name)) &&
        !strncmp(app->version,version,sizeof(app->version)) && !strcmp(actual,elf);
}
esp_err_t dfm_ota_upload(httpd_req_t *r) {
    if(!maintenance_ack(r)) return failure(r,"explicit maintenance acknowledgement required");
    if(status.selected) return failure(r,"a selected update requires reboot before another upload");
    int64_t deadline=esp_timer_get_time()+60000000;
    uint8_t header[16];char manifest[MANIFEST_MAX+1];uint8_t signature[SIGNATURE_MAX],digest[32]={0};
    if(r->content_len<16+288 || r->content_len>0x1c0000+PACKAGE_OVERHEAD ||
       !read_exact(r,header,sizeof(header),deadline) || memcmp(header,"DFMOTA1\n",8)) return failure(r,"invalid package header");
    size_t manifest_size=big32(header+8),signature_size=big32(header+12);
    if(!manifest_size || manifest_size>MANIFEST_MAX || signature_size!=384 ||
       !read_exact(r,(uint8_t *)manifest,manifest_size,deadline) || !read_exact(r,signature,signature_size,deadline))
        return failure(r,"invalid manifest or signature length");
    manifest[manifest_size]=0;
    mbedtls_pk_context key;mbedtls_pk_init(&key);
    bool signature_ok=trusted_key(&key) && mbedtls_sha256((const uint8_t *)manifest,manifest_size,digest,0)==0 &&
        mbedtls_pk_verify(&key,MBEDTLS_MD_SHA256,digest,32,signature,signature_size)==0;
    mbedtls_pk_free(&key);
    if(!signature_ok) return failure(r,"signature or trusted key invalid");
    cJSON *m=cJSON_ParseWithOpts(manifest,NULL,true);
    size_t image_size=0;char expected[65],elf[65],version[32];
    bool valid=manifest_valid(m,&image_size,expected,elf,version);cJSON_Delete(m);
    if(!valid || (size_t)r->content_len!=16+manifest_size+signature_size+image_size) return failure(r,"manifest identity/size/schema invalid");
    if(!read_exact(r,io_buffer,288,deadline) || !image_identity(io_buffer,version,elf)) return failure(r,"image identity does not match signed manifest");
    const esp_partition_t *partition=esp_ota_get_next_update_partition(NULL);
    if(!partition || image_size>partition->size) return failure(r,"no compatible inactive application slot");
    staged_partition=NULL;
    portENTER_CRITICAL(&ota_mux);status.busy=true;status.staged=false;status.received=0;status.total=(uint32_t)image_size;status.error[0]=0;portEXIT_CRITICAL(&ota_mux);
    runtime_log("explicit OTA maintenance: flash activity can delay acquisition/metrics");
    esp_ota_handle_t handle;
    if(esp_ota_begin(partition,image_size,&handle)!=ESP_OK) return failure(r,"OTA begin failed");
    mbedtls_sha256_context sha;mbedtls_sha256_init(&sha);
    bool ok=mbedtls_sha256_starts(&sha,0)==0;
    size_t received=0,next=288;
    while(ok && received<image_size) {
        ok=mbedtls_sha256_update(&sha,io_buffer,next)==0 && esp_ota_write(handle,io_buffer,next)==ESP_OK;
        if(!ok) break;
        received+=next;
        portENTER_CRITICAL(&ota_mux);status.received=(uint32_t)received;portEXIT_CRITICAL(&ota_mux);
        vTaskDelay(pdMS_TO_TICKS(1));
        next=image_size-received;if(next>sizeof(io_buffer)) next=sizeof(io_buffer);
        if(next && !read_exact(r,io_buffer,next,deadline)) ok=false;
    }
    if(ok) ok=mbedtls_sha256_finish(&sha,digest)==0;
    mbedtls_sha256_free(&sha);
    char actual[65];hex(digest,actual);
    if(!ok || mbedtls_ct_memcmp(actual,expected,64)) {
        esp_ota_abort(handle);return failure(r,"interrupted upload, flash write or image hash mismatch");
    }
    if(esp_ota_end(handle)!=ESP_OK) return failure(r,"SDK image validation failed");
    staged_partition=partition;
    portENTER_CRITICAL(&ota_mux);status.busy=false;status.staged=true;
    strcpy(status.staged_sha256,expected);strcpy(status.version,version);portEXIT_CRITICAL(&ota_mux);
    httpd_resp_set_type(r,"application/json");
    char reply[192];snprintf(reply,sizeof(reply),"{\"staged\":true,\"sha256\":\"%s\",\"activated\":false}",actual);
    runtime_log("signed OTA application staged; separate activation required");
    return httpd_resp_sendstr(r,reply);
}
esp_err_t dfm_ota_activate(httpd_req_t *r) {
    if(!maintenance_ack(r) || !staged_partition || !status.staged) return failure(r,"no verified staged update or acknowledgement missing");
    if(!status.selected && esp_ota_set_boot_partition(staged_partition)!=ESP_OK) return failure(r,"boot-slot selection failed");
    portENTER_CRITICAL(&ota_mux);status.selected=true;portEXIT_CRITICAL(&ota_mux);
    dfm_command_t reboot={.type=CMD_REBOOT};bool queued=runtime_queue(&reboot);
    httpd_resp_set_type(r,"application/json");
    return httpd_resp_sendstr(r,queued?"{\"selected\":true,\"reboot_queued\":true}":"{\"selected\":true,\"reboot_queued\":false}");
}
static void health_worker(void *arg) {
    (void)arg;int64_t deadline=esp_timer_get_time()+30000000;
    while(esp_timer_get_time()<deadline) {
        dfm_state_t sample;dfm_config_t config;dfm_runtime_t runtime;
        runtime_snapshot(&sample,&config,&runtime);
        if(sample.successes>=3 && dfm_fresh(&sample,&config,esp_timer_get_time()) && runtime.metrics_ready && runtime.management_ready) {
            esp_err_t result=esp_ota_mark_app_valid_cancel_rollback();
            runtime_log("OTA local health gate: %s",esp_err_to_name(result));
            if(result==ESP_OK) {vTaskDelete(NULL);return;}
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    runtime_log("OTA local health gate failed; requesting rollback");
    esp_ota_mark_app_invalid_rollback_and_reboot();vTaskDelete(NULL);
}
void dfm_ota_health_start(void) {
    esp_ota_img_states_t state;const esp_partition_t *running=esp_ota_get_running_partition();
    if(running && esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY)
        configASSERT(xTaskCreate(health_worker,"dfm_ota_health",4096,NULL,3,NULL)==pdPASS);
}
