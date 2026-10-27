/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
#include <openssl/evp.h>
#include "dfm_core.h"
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_LOG_INFO 3
#define ESP_LOG_DEBUG 4
#define ESP_EVENT_ANY_ID -1
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
#define configASSERT assert
#define ESP_ERROR_CHECK(x) assert((x)==ESP_OK)
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define MALLOC_CAP_8BIT 1
#define GPIO_MODE_OUTPUT 2
#define I2C_NUM_0 0
#define I2C_CLK_SRC_DEFAULT 0
#define I2C_ADDR_BIT_LEN_7 0
#define WIFI_STORAGE_RAM 0
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_MODE_STA 1
#define WIFI_IF_STA 0
#define WIFI_ALL_CHANNEL_SCAN 1
#define WIFI_CONNECT_AP_BY_SIGNAL 0
#define WIFI_EVENT ((void *)1)
#define IP_EVENT ((void *)2)
#define WIFI_EVENT_STA_DISCONNECTED 5
#define IP_EVENT_STA_GOT_IP 0
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define HTTPD_SSL_CONFIG_DEFAULT() ((httpd_ssl_config_t){0})
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) ((p)->addr&255),(((p)->addr>>8)&255),(((p)->addr>>16)&255),(((p)->addr>>24)&255)
#define OTA_SIZE_UNKNOWN ((size_t)-1)
#define ESP_OTA_IMG_PENDING_VERIFY 1
#define ESP_OTA_IMG_VALID 2
#define ESP_CHIP_ID_ESP32C6 13
#define MBEDTLS_MD_SHA256 6
#define MBEDTLS_PK_RSA 1
#define MBEDTLS_PK_SIGNATURE_MAX_SIZE 512
#define MBEDTLS_SHA256_C 1
#define WEAK __attribute__((weak))
typedef int esp_err_t,portMUX_TYPE;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t,*QueueHandle_t;
typedef struct {int unused;} StaticQueue_t;
typedef int nvs_handle_t;
typedef void *i2c_master_bus_handle_t,*i2c_master_dev_handle_t;
typedef int wifi_ps_type_t;
enum {WIFI_PS_NONE,WIFI_PS_MIN_MODEM};
typedef const void *esp_event_base_t;
typedef void (*esp_event_handler_t)(void *,esp_event_base_t,int32_t,void *);
typedef struct {uint32_t addr;} ip4_addr_t;
typedef struct {ip4_addr_t ip,gw,netmask;} esp_netif_ip_info_t;
typedef struct {int unused;} esp_netif_t;
typedef struct {uint8_t reason;} wifi_event_sta_disconnected_t;
typedef struct {esp_netif_ip_info_t ip_info;} ip_event_got_ip_t;
typedef struct {uint8_t bssid[6],primary,ssid[33];int8_t rssi;int authmode;} wifi_ap_record_t;
typedef struct {bool nvs_enable;} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef struct {struct {uint8_t ssid[32],password[64];struct {int authmode;} threshold;int scan_method,sort_method;} sta;} wifi_config_t;
typedef struct {uint64_t pin_bit_mask;int mode;} gpio_config_t;
typedef struct {int i2c_port,sda_io_num,scl_io_num,clk_source,glitch_ignore_cnt;struct {bool enable_internal_pullup;} flags;} i2c_master_bus_config_t;
typedef struct {int dev_addr_length,device_address,scl_speed_hz;} i2c_device_config_t;
typedef struct httpd_req {
 const char *uri;int content_len;const uint8_t *body;size_t offset;
 const char *authorization,*confirmation,*request_type;char reply[65536],status[64],content_type[128];
 int fail_recv_at;size_t reply_length;
} httpd_req_t;
typedef void *httpd_handle_t;
typedef struct {int server_port,task_priority,stack_size,max_open_sockets,max_uri_handlers,ctrl_port,recv_wait_timeout,send_wait_timeout;bool lru_purge_enable;} httpd_config_t;
typedef struct {httpd_config_t httpd;const unsigned char *servercert,*prvtkey_pem;size_t servercert_len,prvtkey_len;} httpd_ssl_config_t;
typedef struct {const char *uri;int method;esp_err_t (*handler)(httpd_req_t *);} httpd_uri_t;
typedef struct {uint32_t magic_word,secure_version,reserved[2];char version[32],project_name[32],time[16],date[16],idf_ver[32];uint8_t app_elf_sha256[32];} esp_app_desc_t;
typedef struct {uint8_t magic,segment_count,spi_mode,spi_speed;uint32_t entry;uint8_t wp_pin,drive[3];uint16_t chip_id;uint8_t rest[10];} esp_image_header_t;
typedef struct {size_t size;char label[16];} esp_partition_t;
typedef int esp_ota_img_states_t;
typedef unsigned esp_ota_handle_t;
typedef struct {EVP_PKEY *key;} mbedtls_pk_context;
typedef struct {EVP_MD_CTX *context;} mbedtls_sha256_context;
extern jmp_buf fake_jump;
extern bool fake_jumping;
extern int fake_steps;
extern int64_t fake_now;
extern void (*fake_tick_hook)(void);
extern dfm_config_t fake_config;
extern dfm_state_t fake_state;
extern uint32_t fake_heap;
extern int fake_nvs_init_result;
extern int fake_nvs_commit_result,fake_nvs_writes,fake_restart_count,fake_ps_result,fake_start_count;
extern int fake_i2c_result,fake_i2c_tx_result,fake_i2c_reads,fake_i2c_bus_result;
extern uint8_t fake_frame[9];
extern int fake_ota_result,fake_ota_state,fake_ota_begin_count,fake_ota_abort_count,fake_ota_end_count,fake_ota_selected,fake_ota_validated,fake_ota_rollback;
extern size_t fake_ota_written;
extern unsigned char fake_ota_image[0x1c0000];
extern esp_partition_t fake_partition;
extern esp_app_desc_t fake_app;
extern int fake_management_started,fake_metrics_started,fake_sensor_started,fake_runtime_started,fake_network_started,fake_health_started;
extern int fake_ssl_result,fake_routes;
extern int (*fake_log_sink)(const char *,va_list);
void fake_reset(void);
void fake_run(void (*worker)(void *),int steps);
void fake_set_text(const char *ns,const char *key,const char *value);
void fake_frame_make(int pressure,int temp,int scale);
void fake_task_run(const char *name,int steps);
int fake_task_priority(const char *name);
const char *esp_err_to_name(esp_err_t);
int64_t esp_timer_get_time(void);
uint32_t esp_random(void);
int esp_reset_reason(void);
const char *esp_get_idf_version(void);
uint32_t esp_get_free_heap_size(void);
uint32_t esp_get_minimum_free_heap_size(void);
size_t heap_caps_get_largest_free_block(int);
void esp_restart(void);
int esp_log_set_vprintf(int (*sink)(const char *,va_list));
void esp_log_level_set(const char *,int);
int xTaskCreate(void (*)(void *),const char *,unsigned,void *,unsigned,TaskHandle_t *);
void vTaskDelay(unsigned);
void vTaskDelayUntil(TickType_t *,TickType_t);
void vTaskDelete(void *);
TickType_t xTaskGetTickCount(void);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t);
QueueHandle_t xQueueCreate(unsigned,unsigned);
QueueHandle_t xQueueCreateStatic(unsigned,unsigned,uint8_t *,StaticQueue_t *);
int xQueueSend(QueueHandle_t,const void *,unsigned);
int xQueueReceive(QueueHandle_t,void *,unsigned);
esp_err_t nvs_open(const char *,int,nvs_handle_t *);
esp_err_t nvs_get_str(nvs_handle_t,const char *,char *,size_t *);
esp_err_t nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);
esp_err_t nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_init_partition(const char *);
esp_err_t nvs_open_from_partition(const char *,const char *,int,nvs_handle_t *);
esp_err_t gpio_config(const gpio_config_t *);
esp_err_t gpio_set_level(int,int);
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *,i2c_master_bus_handle_t *);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t,const i2c_device_config_t *,i2c_master_dev_handle_t *);
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t,const uint8_t *,size_t,int);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t,uint8_t *,size_t,int);
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_err_t esp_netif_set_hostname(esp_netif_t *,const char *);
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_event_handler_register(esp_event_base_t,int,esp_event_handler_t,void *);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int,const wifi_config_t *);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t);
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
size_t httpd_req_get_hdr_value_len(httpd_req_t *,const char *);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *,const char *,char *,size_t);
int httpd_req_recv(httpd_req_t *,char *,size_t);
esp_err_t httpd_resp_set_status(httpd_req_t *,const char *);
esp_err_t httpd_resp_set_type(httpd_req_t *,const char *);
esp_err_t httpd_resp_set_hdr(httpd_req_t *,const char *,const char *);
esp_err_t httpd_resp_sendstr(httpd_req_t *,const char *);
esp_err_t httpd_resp_send(httpd_req_t *,const char *,size_t);
esp_err_t httpd_start(httpd_handle_t *,httpd_config_t *);
esp_err_t httpd_ssl_start(httpd_handle_t *,httpd_ssl_config_t *);
esp_err_t httpd_register_uri_handler(httpd_handle_t,const httpd_uri_t *);
int mbedtls_ct_memcmp(const void *,const void *,size_t);
void mbedtls_pk_init(mbedtls_pk_context *);
void mbedtls_pk_free(mbedtls_pk_context *);
int mbedtls_pk_parse_public_key(mbedtls_pk_context *,const unsigned char *,size_t);
int mbedtls_pk_can_do(const mbedtls_pk_context *,int);
size_t mbedtls_pk_get_bitlen(const mbedtls_pk_context *);
int mbedtls_pk_verify(mbedtls_pk_context *,int,const unsigned char *,size_t,const unsigned char *,size_t);
void mbedtls_sha256_init(mbedtls_sha256_context *);
void mbedtls_sha256_free(mbedtls_sha256_context *);
int mbedtls_sha256_starts(mbedtls_sha256_context *,int);
int mbedtls_sha256_update(mbedtls_sha256_context *,const unsigned char *,size_t);
int mbedtls_sha256_finish(mbedtls_sha256_context *,unsigned char *);
int mbedtls_sha256(const unsigned char *,size_t,unsigned char *,int);
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *,esp_ota_img_states_t *);
esp_err_t esp_ota_begin(const esp_partition_t *,size_t,esp_ota_handle_t *);
esp_err_t esp_ota_write(esp_ota_handle_t,const void *,size_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
const esp_app_desc_t *esp_app_get_description(void);

#define WIFI_EVENT_SCAN_DONE 1234
#define WIFI_SCAN_TYPE_ACTIVE 0
typedef struct {uint8_t *ssid;int scan_type;struct {struct {uint32_t min,max;} active;} scan_time;} wifi_scan_config_t;
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *,bool);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *,wifi_ap_record_t *);
extern wifi_ap_record_t fake_ap,fake_scan_records[16];
extern int fake_scan_result,fake_scan_count,fake_scan_starts,fake_disconnects;
extern bool fake_ota_busy;
