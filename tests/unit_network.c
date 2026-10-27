/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../main/network.c"
#include <assert.h>
int main(void){
 fake_reset();initialized=false;network=(dfm_runtime_t){0};network_start();assert(!initialized);
 assert(network_power_save(true)==ESP_ERR_INVALID_STATE);network_reconnect();
 fake_set_text("dfm_provision","wifi_ssid","unit-test");fake_set_text("dfm_provision","wifi_password","never-logged-secret");
 network_start();assert(initialized && network.provisioned && fake_task_priority("dfm_network")==4);
 assert(network_power_save(true)==ESP_OK);wifi_ps_type_t mode;esp_wifi_get_ps(&mode);assert(mode==WIFI_PS_MIN_MODEM);
 fake_ps_result=ESP_FAIL;assert(network_power_save(false)!=ESP_OK);fake_ps_result=0;network_reconnect();
 event_handler(NULL,WIFI_EVENT,42,NULL); /* unrelated events ignored */
 wifi_event_sta_disconnected_t disconnected={.reason=200};event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&disconnected);
 fake_run(network_worker,6);assert(!network.connected && network.disconnect_reason==200 && network.reconnects>0);
 ip_event_got_ip_t ip={.ip_info={.ip={0x550200c0},.gw={0x010200c0},.netmask={0x00ffffff}}};
 event_handler(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&ip);fake_run(network_worker,4);
 assert(network.connected && network.rssi==-55 && network.channel==6 && !strcmp(network.ip,"192.0.2.85"));
 for(int i=0;i<12;i++)event_handler(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&disconnected);
 fake_run(network_worker,15);assert(!network.connected);
 /* Roaming never scans a healthy link; weak signal must persist. */
 network.connected=true;network.connected_us=1;network.rssi=-55;
 roaming_tick(200000000,false);assert(fake_scan_starts==0);
 network.rssi=-80;fake_ap.rssi=-80;weak_since=0;next_scan=0;
 roaming_tick(200000000,false);roaming_tick(259000000,false);assert(fake_scan_starts==0);
 fake_ota_busy=true;roaming_tick(261000000,false);assert(fake_scan_starts==0);
 fake_ota_busy=false;roaming_tick(262000000,false);assert(scanning && fake_scan_starts==1);
 fake_scan_count=4;
 fake_scan_records[0]=(wifi_ap_record_t){.rssi=-50,.bssid={1},.authmode=WIFI_AUTH_WPA2_PSK};strcpy((char*)fake_scan_records[0].ssid,"wrong-ssid");
 fake_scan_records[1]=(wifi_ap_record_t){.rssi=-55,.bssid={2},.authmode=0};strcpy((char*)fake_scan_records[1].ssid,"unit-test");
 fake_scan_records[2]=(wifi_ap_record_t){.rssi=-69,.bssid={3},.authmode=WIFI_AUTH_WPA2_PSK};strcpy((char*)fake_scan_records[2].ssid,"unit-test");
 fake_scan_records[3]=(wifi_ap_record_t){.rssi=-60,.bssid={4},.authmode=WIFI_AUTH_WPA2_PSK};strcpy((char*)fake_scan_records[3].ssid,"unit-test");
 int before=fake_disconnects;roaming_tick(263000000,true);assert(!scanning && network.roam_attempts==1 && fake_disconnects==before+1);
 roaming_tick(400000000,false);assert(fake_scan_starts==1); /* cooldown */
 weak_since=1;roaming_tick(563000000,false);assert(scanning);
 fake_ota_busy=true;roaming_tick(564000000,false);assert(!scanning && network.roam_scan_errors==1);fake_ota_busy=false;
 next_scan=0;roaming_tick(565000000,false);assert(scanning);roaming_tick(571000001,false);assert(!scanning); /* timeout */
 next_scan=0;fake_scan_result=ESP_FAIL;roaming_tick(572000000,false);assert(!scanning);
 fake_scan_result=0;next_scan=0;roaming_tick(573000000,false);assert(scanning);
 fake_scan_result=ESP_FAIL;roaming_tick(574000000,true);assert(!scanning); /* retrieval failure */
 fake_scan_result=0;next_scan=0;roaming_tick(575000000,false);fake_scan_count=0;roaming_tick(576000000,true);assert(!scanning && network.roam_attempts==1);
 /* A small gain, weak candidate, same BSSID, or recovered current link never causes roaming. */
 fake_scan_count=1;fake_scan_records[0]=fake_scan_records[3];fake_scan_records[0].rssi=-69;
 fake_ap.rssi=-80;network.rssi=-80;next_scan=0;weak_since=1;
 roaming_tick(600000000,false);roaming_tick(601000000,true);assert(network.roam_attempts==1);
 fake_scan_records[0].rssi=-79;fake_ap.rssi=-95;next_scan=0;roaming_tick(602000000,false);roaming_tick(603000000,true);assert(network.roam_attempts==1);
 fake_ap.rssi=-80;fake_scan_records[0].rssi=-50;memcpy(fake_scan_records[0].bssid,fake_ap.bssid,6);
 next_scan=0;roaming_tick(604000000,false);roaming_tick(605000000,true);assert(network.roam_attempts==1);
 fake_scan_records[0].bssid[0]=9;next_scan=0;roaming_tick(606000000,false);
 fake_ap.rssi=-60;roaming_tick(607000000,true);assert(network.roam_attempts==1);
 /* SDK 5.5.6 unknown/unparseable authentication is not a secure candidate. */
 network.rssi=-80;fake_ap.rssi=-80;weak_since=1;next_scan=0;
 fake_scan_records[0].authmode=WIFI_AUTH_UNKNOWN;
 roaming_tick(608000000,false);roaming_tick(609000000,true);assert(!scanning && network.roam_attempts==1);
 fake_scan_records[0].authmode=WIFI_AUTH_MAX;next_scan=0;
 roaming_tick(610000000,false);roaming_tick(611000000,true);assert(!scanning && network.roam_attempts==1);
 network.connected=false;next_scan=0;roaming_tick(612000000,false);assert(!scanning && weak_since==0);
 event_handler(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,NULL);fake_run(network_worker,2);
 puts("PASS network: no implicit stock credentials, pins, sleep readback, reconnects, events, overflow");return 0;
}
