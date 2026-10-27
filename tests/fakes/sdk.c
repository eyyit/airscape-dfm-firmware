/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "fake_sdk.h"
#include "runtime.h"
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/crypto.h>
#include <openssl/sha.h>
jmp_buf fake_jump;bool fake_jumping;int fake_steps;int64_t fake_now;
void (*fake_tick_hook)(void);
dfm_config_t fake_config;dfm_state_t fake_state;uint32_t fake_heap;
int fake_nvs_init_result;
int fake_nvs_commit_result,fake_nvs_writes,fake_restart_count,fake_ps_result,fake_start_count;
int fake_i2c_result,fake_i2c_tx_result,fake_i2c_reads,fake_i2c_bus_result;
uint8_t fake_frame[9];
int fake_ota_result,fake_ota_state,fake_ota_begin_count,fake_ota_abort_count,fake_ota_end_count,fake_ota_selected,fake_ota_validated,fake_ota_rollback;
size_t fake_ota_written;unsigned char fake_ota_image[0x1c0000];
esp_partition_t fake_partition;esp_app_desc_t fake_app;
int fake_management_started,fake_metrics_started,fake_sensor_started,fake_runtime_started,fake_network_started,fake_health_started;
int fake_ssl_result,fake_routes;int (*fake_log_sink)(const char *,va_list);
wifi_ap_record_t fake_ap,fake_scan_records[16];
int fake_scan_result,fake_scan_count,fake_scan_starts,fake_disconnects;
bool fake_ota_busy;
static wifi_ps_type_t ps;
typedef struct {unsigned size,length,head,count;uint8_t data[8][512];} queue_t;
static queue_t queues[16];static unsigned queue_count;
typedef struct {char ns[32],key[32];uint8_t value[4096];size_t length;} value_t;
static value_t values[32];static unsigned value_count;
static char namespaces[16][32];static unsigned namespace_count;
typedef struct {void (*fn)(void *);void *arg;char name[32];int priority;} task_t;
static task_t tasks[16];static unsigned task_count;
WEAK TaskHandle_t sensor_task_handle,metrics_task_handle;
void fake_reset(void){
 fake_ap=(wifi_ap_record_t){.rssi=-55,.primary=6};fake_scan_result=fake_scan_count=fake_scan_starts=fake_disconnects=0;fake_ota_busy=false;
 fake_now=1000000;fake_steps=100;fake_jumping=false;fake_tick_hook=NULL;
 fake_config=dfm_defaults();memset(&fake_state,0,sizeof(fake_state));fake_heap=1000000;
 fake_nvs_init_result=0;fake_nvs_commit_result=0;fake_nvs_writes=0;fake_restart_count=0;fake_ps_result=0;fake_start_count=0;
 fake_i2c_result=0;fake_i2c_tx_result=0;fake_i2c_reads=0;fake_i2c_bus_result=0;
 fake_ota_result=0;fake_ota_state=ESP_OTA_IMG_VALID;fake_ota_begin_count=0;fake_ota_abort_count=0;fake_ota_end_count=0;fake_ota_selected=0;fake_ota_validated=0;fake_ota_rollback=0;fake_ota_written=0;
 fake_partition=(esp_partition_t){.size=0x1c0000,.label="ota_1"};memset(&fake_app,0,sizeof(fake_app));strcpy(fake_app.version,"0.1.0-dev");strcpy(fake_app.project_name,"airscape_dfm");
 fake_management_started=fake_metrics_started=fake_sensor_started=fake_runtime_started=fake_network_started=fake_health_started=0;
 fake_ssl_result=fake_routes=0;fake_log_sink=NULL;ps=0;queue_count=task_count=value_count=namespace_count=0;
 memset(queues,0,sizeof(queues));memset(values,0,sizeof(values));memset(tasks,0,sizeof(tasks));
}
static void step(unsigned ms){fake_now+=(int64_t)ms*1000;if(fake_tick_hook)fake_tick_hook();if(fake_jumping && --fake_steps<=0)longjmp(fake_jump,1);}
void fake_run(void (*fn)(void *),int steps){fake_steps=steps;fake_jumping=true;if(!setjmp(fake_jump))fn(NULL);fake_jumping=false;}
void fake_task_run(const char *name,int steps){for(unsigned i=0;i<task_count;i++)if(!strcmp(tasks[i].name,name)){fake_run(tasks[i].fn,steps);return;}assert(!"missing task");}
int fake_task_priority(const char *name){for(unsigned i=0;i<task_count;i++)if(!strcmp(tasks[i].name,name))return tasks[i].priority;return -1;}
const char *esp_err_to_name(esp_err_t e){return e==0?"ESP_OK":"FAKE_ERROR";}
int64_t esp_timer_get_time(void){return fake_now;}
uint32_t esp_random(void){return 42;}
int esp_reset_reason(void){return 1;}
const char *esp_get_idf_version(void){return "fake-sdk";}
uint32_t esp_get_free_heap_size(void){return fake_heap;}
uint32_t esp_get_minimum_free_heap_size(void){return fake_heap/2;}
size_t heap_caps_get_largest_free_block(int cap){(void)cap;return fake_heap/2;}
void esp_restart(void){fake_restart_count++;if(fake_jumping)longjmp(fake_jump,1);}
int esp_log_set_vprintf(int (*sink)(const char *,va_list)){fake_log_sink=sink;return 0;}
void esp_log_level_set(const char *tag,int level){(void)tag;(void)level;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle){(void)stack;assert(task_count<16);task_t *t=&tasks[task_count++];*t=(task_t){.fn=fn,.arg=arg,.priority=(int)priority};snprintf(t->name,sizeof(t->name),"%s",name);if(handle)*handle=t;return pdPASS;}
void vTaskDelay(unsigned ms){step(ms);}
void vTaskDelayUntil(TickType_t *wake,TickType_t ticks){*wake+=ticks;step(ticks);}
void vTaskDelete(void *arg){(void)arg;if(fake_jumping)longjmp(fake_jump,1);}
TickType_t xTaskGetTickCount(void){return (TickType_t)(fake_now/1000);}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t task){(void)task;return 1024;}
QueueHandle_t xQueueCreate(unsigned length,unsigned size){assert(queue_count<16 && length<=8 && size<=512);queue_t *q=&queues[queue_count++];q->size=size;q->length=length;return q;}
QueueHandle_t xQueueCreateStatic(unsigned n,unsigned size,uint8_t *storage,StaticQueue_t *control){(void)storage;(void)control;return xQueueCreate(n,size);}
int xQueueSend(QueueHandle_t handle,const void *data,unsigned wait){(void)wait;queue_t *q=handle;if(!q || q->count==q->length)return 0;memcpy(q->data[(q->head+q->count)%q->length],data,q->size);q->count++;return 1;}
int xQueueReceive(QueueHandle_t handle,void *out,unsigned wait){queue_t *q=handle;step(q->count?0:wait);if(!q->count)return 0;memcpy(out,q->data[q->head],q->size);q->head=(q->head+1)%q->length;q->count--;return 1;}
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *out){for(unsigned i=0;i<namespace_count;i++)if(!strcmp(namespaces[i],ns)){*out=(int)i+1;return 0;}if(!mode)return ESP_ERR_NVS_NOT_FOUND;assert(namespace_count<16);snprintf(namespaces[namespace_count],32,"%s",ns);*out=++namespace_count;return 0;}
static value_t *find_value(nvs_handle_t h,const char *key){assert(h>0 && (unsigned)h<=namespace_count);for(unsigned i=0;i<value_count;i++)if(!strcmp(values[i].ns,namespaces[h-1]) && !strcmp(values[i].key,key))return &values[i];return NULL;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size){value_t *v=find_value(h,key);if(!v)return ESP_ERR_NVS_NOT_FOUND;if(out && *size<v->length)return ESP_FAIL;if(out)memcpy(out,v->value,v->length);*size=v->length;return 0;}
esp_err_t nvs_get_str(nvs_handle_t h,const char *key,char *out,size_t *size){return nvs_get_blob(h,key,out,size);}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *value,size_t size){value_t *v=find_value(h,key);if(!v){assert(value_count<32);v=&values[value_count++];snprintf(v->ns,32,"%s",namespaces[h-1]);snprintf(v->key,32,"%s",key);}assert(size<=sizeof(v->value));memcpy(v->value,value,size);v->length=size;fake_nvs_writes++;return 0;}
void fake_set_text(const char *ns,const char *key,const char *value){nvs_handle_t h;assert(nvs_open(ns,1,&h)==0);assert(nvs_set_blob(h,key,value,strlen(value)+1)==0);}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;return fake_nvs_commit_result;}
void nvs_close(nvs_handle_t h){(void)h;}
esp_err_t nvs_flash_init(void){return 0;}
esp_err_t gpio_config(const gpio_config_t *c){assert(c->mode==GPIO_MODE_OUTPUT);return 0;}
esp_err_t gpio_set_level(int pin,int value){assert(pin==3 || pin==14 || pin==15);assert(value==0 || value==1);return 0;}
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *c,i2c_master_bus_handle_t *h){assert(c->sda_io_num==22 && c->scl_io_num==23);*h=(void *)1;return fake_i2c_bus_result;}
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t b,const i2c_device_config_t *c,i2c_master_dev_handle_t *h){(void)b;assert(c->device_address==0x25 && c->scl_speed_hz==100000);*h=(void *)2;return 0;}
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t b){(void)b;return 0;}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t d,const uint8_t *b,size_t n,int timeout){(void)d;(void)b;assert(n==2 && timeout>0);return fake_i2c_tx_result;}
esp_err_t i2c_master_receive(i2c_master_dev_handle_t d,uint8_t *b,size_t n,int timeout){(void)d;assert(n==9 && timeout>0);fake_i2c_reads++;memcpy(b,fake_frame,n);return fake_i2c_result;}
void fake_frame_make(int p,int t,int s){int words[]={p,t,s};for(unsigned i=0;i<3;i++){fake_frame[3*i]=(uint16_t)words[i]>>8;fake_frame[3*i+1]=(uint8_t)words[i];fake_frame[3*i+2]=dfm_crc(fake_frame+3*i,2);}}
esp_err_t esp_netif_init(void){return 0;}
esp_err_t esp_event_loop_create_default(void){return 0;}
esp_netif_t *esp_netif_create_default_wifi_sta(void){static esp_netif_t n;return &n;}
esp_err_t esp_netif_set_hostname(esp_netif_t *n,const char *s){(void)n;(void)s;return 0;}
esp_err_t esp_wifi_init(const wifi_init_config_t *c){assert(!c->nvs_enable);return 0;}
esp_err_t esp_wifi_set_storage(int s){assert(s==WIFI_STORAGE_RAM);return 0;}
esp_err_t esp_event_handler_register(esp_event_base_t base,int id,esp_event_handler_t h,void *p){(void)base;(void)id;(void)h;(void)p;return 0;}
esp_err_t esp_wifi_set_mode(int mode){assert(mode==WIFI_MODE_STA);return 0;}
esp_err_t esp_wifi_set_config(int id,const wifi_config_t *c){assert(id==WIFI_IF_STA && c->sta.scan_method==WIFI_ALL_CHANNEL_SCAN && c->sta.sort_method==WIFI_CONNECT_AP_BY_SIGNAL);return 0;}
esp_err_t esp_wifi_start(void){return 0;}
esp_err_t esp_wifi_set_ps(wifi_ps_type_t value){ps=value;return fake_ps_result;}
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *out){*out=ps;return fake_ps_result;}
esp_err_t esp_wifi_disconnect(void){fake_disconnects++;return 0;}
esp_err_t esp_wifi_connect(void){fake_start_count++;return 0;}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *out){*out=fake_ap;return 0;}
static const char *header(httpd_req_t *r,const char *key){if(!strcmp(key,"Content-Type"))return r->request_type;if(!strcmp(key,"Authorization"))return r->authorization;if(!strcmp(key,"X-DFM-Maintenance"))return r->confirmation;return NULL;}
size_t httpd_req_get_hdr_value_len(httpd_req_t *r,const char *key){const char *s=header(r,key);return s?strlen(s):0;}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *out,size_t size){const char *s=header(r,key);if(!s || strlen(s)>=size)return ESP_FAIL;strcpy(out,s);return 0;}
int httpd_req_recv(httpd_req_t *r,char *out,size_t size){if(r->fail_recv_at>0 && r->offset>=(size_t)r->fail_recv_at)return -1;size_t n=(size_t)r->content_len-r->offset;if(n>size)n=size;if(n>4096)n=4096;memcpy(out,r->body+r->offset,n);r->offset+=n;return (int)n;}
esp_err_t httpd_resp_set_status(httpd_req_t *r,const char *s){snprintf(r->status,sizeof(r->status),"%s",s);return 0;}
esp_err_t httpd_resp_set_type(httpd_req_t *r,const char *s){snprintf(r->content_type,sizeof(r->content_type),"%s",s);return 0;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *r,const char *key,const char *value){(void)r;(void)key;(void)value;return 0;}
esp_err_t httpd_resp_send(httpd_req_t *r,const char *s,size_t n){assert(n<sizeof(r->reply));memcpy(r->reply,s,n);r->reply[n]=0;r->reply_length=n;return 0;}
esp_err_t httpd_resp_sendstr(httpd_req_t *r,const char *s){return httpd_resp_send(r,s,strlen(s));}
esp_err_t httpd_ssl_start(httpd_handle_t *s,httpd_ssl_config_t *c){assert(c->httpd.task_priority==2);*s=(void *)1;return fake_ssl_result;}
esp_err_t httpd_register_uri_handler(httpd_handle_t s,const httpd_uri_t *u){(void)s;assert(u->handler);fake_routes++;return 0;}
int mbedtls_ct_memcmp(const void *a,const void *b,size_t n){return CRYPTO_memcmp(a,b,n);}
void mbedtls_pk_init(mbedtls_pk_context *c){c->key=NULL;}
void mbedtls_pk_free(mbedtls_pk_context *c){EVP_PKEY_free(c->key);c->key=NULL;}
int mbedtls_pk_parse_public_key(mbedtls_pk_context *c,const unsigned char *s,size_t n){BIO *bio=BIO_new_mem_buf(s,(int)n);c->key=PEM_read_bio_PUBKEY(bio,NULL,NULL,NULL);BIO_free(bio);return c->key?0:-1;}
int mbedtls_pk_can_do(const mbedtls_pk_context *c,int kind){return kind==MBEDTLS_PK_RSA && c->key && EVP_PKEY_is_a(c->key,"RSA");}
size_t mbedtls_pk_get_bitlen(const mbedtls_pk_context *c){return (size_t)EVP_PKEY_get_bits(c->key);}
int mbedtls_pk_verify(mbedtls_pk_context *c,int md,const unsigned char *hash,size_t n,const unsigned char *sig,size_t len){(void)md;if(!n)n=32;EVP_PKEY_CTX *ctx=EVP_PKEY_CTX_new(c->key,NULL);int ok=ctx && EVP_PKEY_verify_init(ctx)>0 && EVP_PKEY_CTX_set_rsa_padding(ctx,RSA_PKCS1_PADDING)>0 && EVP_PKEY_CTX_set_signature_md(ctx,EVP_sha256())>0 && EVP_PKEY_verify(ctx,sig,len,hash,n)>0;EVP_PKEY_CTX_free(ctx);return ok?0:-1;}
void mbedtls_sha256_init(mbedtls_sha256_context *c){c->context=NULL;}
void mbedtls_sha256_free(mbedtls_sha256_context *c){EVP_MD_CTX_free(c->context);c->context=NULL;}
int mbedtls_sha256_starts(mbedtls_sha256_context *c,int bits){(void)bits;c->context=EVP_MD_CTX_new();return c->context && EVP_DigestInit_ex(c->context,EVP_sha256(),NULL)?0:-1;}
int mbedtls_sha256_update(mbedtls_sha256_context *c,const unsigned char *p,size_t n){return EVP_DigestUpdate(c->context,p,n)?0:-1;}
int mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char *out){return EVP_DigestFinal_ex(c->context,out,NULL)?0:-1;}
int mbedtls_sha256(const unsigned char *p,size_t n,unsigned char *out,int bits){(void)bits;return SHA256(p,n,out)?0:-1;}
const esp_partition_t *esp_ota_get_running_partition(void){return &fake_partition;}
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *p){(void)p;return &fake_partition;}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p,esp_ota_img_states_t *s){(void)p;*s=fake_ota_state;return 0;}
esp_err_t esp_ota_begin(const esp_partition_t *p,size_t n,esp_ota_handle_t *h){assert(n<=p->size);fake_ota_begin_count++;fake_ota_written=0;*h=1;return fake_ota_result;}
esp_err_t esp_ota_write(esp_ota_handle_t h,const void *p,size_t n){(void)h;assert(fake_ota_written+n<=sizeof(fake_ota_image));memcpy(fake_ota_image+fake_ota_written,p,n);fake_ota_written+=n;return fake_ota_result;}
esp_err_t esp_ota_end(esp_ota_handle_t h){(void)h;fake_ota_end_count++;return fake_ota_result;}
esp_err_t esp_ota_abort(esp_ota_handle_t h){(void)h;fake_ota_abort_count++;return 0;}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p){(void)p;fake_ota_selected++;return fake_ota_result;}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void){fake_ota_validated++;fake_ota_state=ESP_OTA_IMG_VALID;return 0;}
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void){fake_ota_rollback++;return 0;}
const esp_app_desc_t *esp_app_get_description(void){return &fake_app;}
WEAK void runtime_init(void){fake_runtime_started++;}
WEAK void runtime_snapshot(dfm_state_t *s,dfm_config_t *c,dfm_runtime_t *r){if(s)*s=fake_state;if(c)*c=fake_config;if(r){memset(r,0,sizeof(*r));r->metrics_ready=true;r->management_ready=true;r->revision=1;r->free_heap=fake_heap;}}
WEAK void runtime_config_read(dfm_config_t *c){*c=fake_config;}
WEAK bool runtime_sensor_config_boundary(dfm_config_t *c){*c=fake_config;return false;}
WEAK void runtime_publish(const dfm_state_t *s){fake_state=*s;}
WEAK bool runtime_take_sensor_action(bool *f){*f=false;return false;}
WEAK void runtime_log(const char *f,...){(void)f;}
WEAK void runtime_update_network(const dfm_runtime_t *n){(void)n;}
WEAK void runtime_management_ready(bool ready){(void)ready;}
WEAK void runtime_primary_ready(bool ready){(void)ready;}
WEAK void runtime_metric_result(bool r,bool e,uint32_t d){(void)r;(void)e;(void)d;}
WEAK void runtime_reject_management(void){}
WEAK bool runtime_queue(dfm_command_t *c){(void)c;return true;}
WEAK size_t runtime_logs(dfm_log_t *l,size_t n,uint64_t *d){(void)l;(void)n;*d=0;return 0;}
WEAK size_t runtime_history(dfm_history_t *h,size_t n){(void)h;(void)n;return 0;}
WEAK void sensor_start(void){fake_sensor_started++;}
WEAK void network_start(void){fake_network_started++;}
WEAK esp_err_t network_power_save(bool enable){(void)enable;return fake_ps_result;}
WEAK void network_reconnect(void){fake_start_count++;}
WEAK void metrics_start(void){fake_metrics_started++;}
WEAK void management_start(void){fake_management_started++;}
WEAK void dfm_ota_health_start(void){fake_health_started++;}

#include "ota.h"
WEAK void dfm_ota_status(dfm_ota_status_t *s){memset(s,0,sizeof(*s));s->busy=fake_ota_busy;}
WEAK esp_err_t dfm_ota_upload(httpd_req_t *r){(void)r;return 0;}
WEAK esp_err_t dfm_ota_activate(httpd_req_t *r){(void)r;return 0;}

esp_err_t nvs_flash_init_partition(const char *partition){assert(!strcmp(partition,"dfm_data"));return fake_nvs_init_result;}
esp_err_t nvs_open_from_partition(const char *partition,const char *ns,int mode,nvs_handle_t *out){assert(!strcmp(partition,"dfm_data"));return nvs_open(ns,mode,out);}

esp_err_t httpd_start(httpd_handle_t *s,httpd_config_t *c){assert(c->task_priority==2 && c->server_port==8080);*s=(void *)1;return fake_ssl_result;}

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *c,bool block){assert(!block && c->scan_time.active.max==40);fake_scan_starts++;return fake_scan_result;}
esp_err_t esp_wifi_scan_stop(void){return 0;}
esp_err_t esp_wifi_clear_ap_list(void){return 0;}
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *n,wifi_ap_record_t *p){if(*n>(unsigned)fake_scan_count)*n=fake_scan_count;memcpy(p,fake_scan_records,*n*sizeof(*p));return fake_scan_result;}

WEAK void syslog_start(void){}
WEAK void runtime_syslog_result(uint64_t s,uint64_t e,uint64_t d){(void)s;(void)e;(void)d;}
WEAK size_t runtime_logs_after(uint64_t c,dfm_log_t *o,size_t n,uint64_t *l){(void)c;(void)o;(void)n;*l=0;return 0;}
