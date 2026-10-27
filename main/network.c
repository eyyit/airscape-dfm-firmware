/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "runtime.h"
#include "device_identity.h"
#include "ota.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/queue.h"
#include "nvs.h"
#include <stdio.h>
#include <string.h>

typedef struct { int type, reason; esp_netif_ip_info_t ip; } network_event_t;
static QueueHandle_t events;
static dfm_runtime_t network;
static bool initialized;
/* Fixed bounded policy: no constant scans and no hard BSSID pin/failover trap. */
#define ROAM_WEAK_DBM (-75)
#define ROAM_MIN_DBM (-78)
#define ROAM_GAIN_DB 12
#define ROAM_WEAK_US (60LL*1000000)
#define ROAM_COOLDOWN_US (300LL*1000000)
static int64_t weak_since, scan_started, next_scan;
static bool scanning;
static uint8_t station_ssid[33];
static wifi_ap_record_t roam_records[16];
static void roaming_tick(int64_t now,bool scan_done) {
    dfm_ota_status_t ota;dfm_ota_status(&ota);
    if(scanning && (!network.connected || ota.busy || ota.selected || now-scan_started>5000000)) {
        esp_wifi_scan_stop();esp_wifi_clear_ap_list();scanning=false;
        network.roam_scan_errors++;return;
    }
    if(scanning && scan_done) {
        wifi_ap_record_t *records=roam_records;uint16_t count=16;scanning=false;
        if(esp_wifi_scan_get_ap_records(&count,records)!=ESP_OK) {
            esp_wifi_clear_ap_list();network.roam_scan_errors++;return;
        }
        wifi_ap_record_t current;
        if(esp_wifi_sta_get_ap_info(&current)!=ESP_OK) return;
        int best=-1;
        for(unsigned i=0;i<count;i++) {
            wifi_ap_record_t *ap=&records[i];
            if(memcmp(ap->bssid,current.bssid,6)==0 || strcmp((char *)ap->ssid,(char *)station_ssid)!=0 ||
               ap->authmode<WIFI_AUTH_WPA2_PSK || ap->rssi<ROAM_MIN_DBM ||
               ap->rssi-current.rssi<ROAM_GAIN_DB) continue;
            if(best<0 || ap->rssi>records[best].rssi) best=(int)i;
        }
        if(best>=0 && current.rssi<=ROAM_WEAK_DBM) {
            wifi_ap_record_t *ap=&records[best];
            runtime_log("Roam candidate %02x:%02x:%02x:%02x:%02x:%02x (%d dBm), current %s (%d dBm)",
                ap->bssid[0],ap->bssid[1],ap->bssid[2],ap->bssid[3],ap->bssid[4],ap->bssid[5],ap->rssi,network.bssid,current.rssi);
            if(esp_wifi_disconnect()==ESP_OK) {network.roam_attempts++;weak_since=0;}
            else network.roam_scan_errors++;
            /* Existing all-channel/signal-sorted connect chooses best available;
               never pin a candidate that may have disappeared since the scan. */
        }
    }
    if(!network.connected || network.rssi>ROAM_WEAK_DBM) {weak_since=0;return;}
    if(!weak_since) weak_since=now;
    if(scanning || ota.busy || ota.selected || ota.pending_verify || now<next_scan ||
       now-weak_since<ROAM_WEAK_US || now-(int64_t)network.connected_us<120000000) return;
    wifi_scan_config_t config={0};
    config.ssid=station_ssid;config.scan_type=WIFI_SCAN_TYPE_ACTIVE;
    config.scan_time.active.min=20;config.scan_time.active.max=40;
    next_scan=now+ROAM_COOLDOWN_US;
    if(esp_wifi_scan_start(&config,false)==ESP_OK) {
        scanning=true;scan_started=now;network.roam_scans++;
    } else network.roam_scan_errors++;
}
static void event_handler(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;
    network_event_t event={0};
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        event.type=1;event.reason=((wifi_event_sta_disconnected_t *)data)->reason;
    } else if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        event.type=2;event.ip=((ip_event_got_ip_t *)data)->ip_info;
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE) {event.type=3;} else return;
    if(xQueueSend(events,&event,0)!=pdTRUE) runtime_log("network event dropped: queue full");
}
esp_err_t network_power_save(bool enabled) {
    if(!initialized) return ESP_ERR_INVALID_STATE;
    wifi_ps_type_t actual;
    esp_err_t err=esp_wifi_set_ps(enabled?WIFI_PS_MIN_MODEM:WIFI_PS_NONE);
    if(err==ESP_OK) err=esp_wifi_get_ps(&actual);
    if(err==ESP_OK && actual!=(enabled?WIFI_PS_MIN_MODEM:WIFI_PS_NONE)) err=ESP_FAIL;
    return err;
}
void network_reconnect(void) {
    if(initialized) esp_wifi_disconnect();
}
static void network_worker(void *arg) {
    (void)arg;
    unsigned failures=0;
    int64_t retry_at=esp_timer_get_time();
    for(;;) {
        network_event_t event;bool scan_done=false;
        if(xQueueReceive(events,&event,pdMS_TO_TICKS(500))==pdTRUE) {
            if(event.type==1) {
                network.connected=false;network.connected_us=0;network.rssi=-127;
                network.disconnect_reason=event.reason; network.ip[0]=0;
                unsigned backoff=1u<<(failures<5?failures:5);failures++;
                retry_at=esp_timer_get_time()+backoff*1000000LL+(esp_random()%250000);
                runtime_log("Wi-Fi disconnected reason=%d; reconnect in %u seconds",event.reason,backoff);
            } else if(event.type==3) {scan_done=true;} else {
                network.connected=true;network.connected_us=esp_timer_get_time();failures=0;retry_at=0;
                snprintf(network.ip,sizeof(network.ip),IPSTR,IP2STR(&event.ip.ip));
                snprintf(network.gateway,sizeof(network.gateway),IPSTR,IP2STR(&event.ip.gw));
                snprintf(network.netmask,sizeof(network.netmask),IPSTR,IP2STR(&event.ip.netmask));
                dfm_config_t config;runtime_config_read(&config);
                runtime_log("Wi-Fi connected; applying requested sleep=%d result=%s",config.power_save,
                    esp_err_to_name(network_power_save(config.power_save)));
            }
        }
        if(!network.connected && retry_at && esp_timer_get_time()>=retry_at) {
            network.reconnects++;retry_at=esp_timer_get_time()+15000000;
            esp_err_t result=esp_wifi_connect();
            if(result!=ESP_OK) runtime_log("Wi-Fi connect: %s",esp_err_to_name(result));
        }
        if(network.connected) {
            wifi_ap_record_t ap;
            if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK) {
                network.rssi=ap.rssi;network.channel=ap.primary;
                char bssid[18];snprintf(bssid,sizeof(bssid),"%02x:%02x:%02x:%02x:%02x:%02x",
                    ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5]);
                if(strcmp(network.bssid,bssid)) runtime_log("Wi-Fi AP %s -> %s channel=%d RSSI=%d",network.bssid,bssid,ap.primary,ap.rssi);
                snprintf(network.bssid,sizeof(network.bssid),"%02x:%02x:%02x:%02x:%02x:%02x",
                    ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5]);
            }
            wifi_ps_type_t ps;
            if(esp_wifi_get_ps(&ps)==ESP_OK) network.power_save_applied=ps!=WIFI_PS_NONE;
        }
        roaming_tick(esp_timer_get_time(),scan_done);
        runtime_update_network(&network);
    }
}
void network_start(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    gpio_config_t antenna={.pin_bit_mask=(1ULL<<3)|(1ULL<<14),.mode=GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&antenna));
    gpio_set_level(3,1);gpio_set_level(14,0);gpio_set_level(3,0);
    /* Board mapping established; status LED remains off for this build. */
    gpio_config_t led={.pin_bit_mask=1ULL<<15,.mode=GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&led));gpio_set_level(15,1);
    char ssid[33]={0},password[65]={0};
    nvs_handle_t nvs;size_t len=sizeof(ssid);
    if(nvs_open_from_partition("dfm_data","dfm_provision",NVS_READONLY,&nvs)==ESP_OK) {
        esp_err_t result=nvs_get_str(nvs,"wifi_ssid",ssid,&len);
        len=sizeof(password);nvs_get_str(nvs,"wifi_password",password,&len);
        if(result!=ESP_OK) ssid[0]=0;
        nvs_close(nvs);
    }
    memcpy(station_ssid,ssid,sizeof(station_ssid));
    network.rssi=-127;
    if(!ssid[0]) {runtime_log("unprovisioned: Wi-Fi disabled; stock credentials never imported");runtime_update_network(&network);return;}
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *netif=esp_netif_create_default_wifi_sta();configASSERT(netif);
    ESP_ERROR_CHECK(esp_netif_set_hostname(netif,DFM_DEVICE_SHORTNAME));
    events=xQueueCreate(8,sizeof(network_event_t));configASSERT(events);
    wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();
    config.nvs_enable=false; /* Keep SDK Wi-Fi storage out of stock NVS too. */
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event_handler,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event_handler,NULL));
    wifi_config_t station={0};
    memcpy(station.sta.ssid,ssid,strlen(ssid));memcpy(station.sta.password,password,strlen(password));
    station.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
    /* A fast scan can settle on the first weak AP. Scan once at connection
       startup and select the strongest matching AP; no diagnostic scans. */
    station.sta.scan_method=WIFI_ALL_CHANNEL_SCAN;
    station.sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&station));
    memset(password,0,sizeof(password));memset(&station,0,sizeof(station));
    ESP_ERROR_CHECK(esp_wifi_start());initialized=true;
    dfm_config_t settings;runtime_config_read(&settings);
    ESP_ERROR_CHECK(network_power_save(settings.power_save));
    network.provisioned=true;runtime_update_network(&network);
    configASSERT(xTaskCreate(network_worker,"dfm_network",4096,NULL,4,NULL)==pdPASS);
}
