/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#ifdef DFM_HOST
#include "../tests/host_runtime.h"
#else
#include "runtime.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#ifndef DFM_METRICS_PORT
#define DFM_METRICS_PORT 80
#endif
#ifdef DFM_HOST
#define DFM_LISTEN_ADDRESS INADDR_LOOPBACK
#else
#define DFM_LISTEN_ADDRESS INADDR_ANY
#endif
/* Primary listener has its own sockets, fixed buffers and task. Never calls the
   management server, JSON allocator, sensor driver, NVS or diagnostic worker. */
#define METRIC_CLIENTS 4
#define METRIC_BUFFER 8192
typedef struct {
    int fd;
    int64_t accepted;
    size_t received, length, sent;
    bool ready;
    char request[2048], response[METRIC_BUFFER];
} metric_client_t;
static metric_client_t clients[METRIC_CLIENTS];

static void close_client(metric_client_t *client,bool error) {
    uint32_t duration=(uint32_t)(esp_timer_get_time()-client->accepted);
    runtime_metric_result(false,error,duration);
    close(client->fd);client->fd=-1;
}
static void prepare_response(metric_client_t *client) {
    const size_t reserve=512;
    char *body=client->response+reserve;
    size_t length=0;
    const char *status="200 OK", *content="text/plain; version=0.0.4; charset=utf-8";
    if(strncmp(client->request,"GET /metrics HTTP/1.",20)==0) {
        dfm_state_t state;dfm_config_t config;dfm_runtime_t r;
        runtime_snapshot(&state,&config,&r);
        if(!dfm_metrics(body,METRIC_BUFFER-reserve,&state,&config,esp_timer_get_time(),&length)) {
            status="503 Service Unavailable";length=0;
        } else {
            int n=snprintf(body+length,METRIC_BUFFER-reserve-length,
                "# HELP hvac_filter_wifi_rssi_dbm Connected station RSSI.\n"
                "# TYPE hvac_filter_wifi_rssi_dbm gauge\n%s"
                "# HELP hvac_filter_wifi_connected Wi-Fi connectivity (0/1).\n"
                "# TYPE hvac_filter_wifi_connected gauge\nhvac_filter_wifi_connected %d\n"
                "# HELP hvac_filter_wifi_power_save_enabled Actual SDK power-save state (0/1).\n"
                "# TYPE hvac_filter_wifi_power_save_enabled gauge\nhvac_filter_wifi_power_save_enabled %d\n"
                "# HELP hvac_filter_wifi_reconnects_total Station connection attempts.\n"
                "# TYPE hvac_filter_wifi_reconnects_total counter\nhvac_filter_wifi_reconnects_total %llu\n"
                "# HELP hvac_filter_wifi_roam_scans_total Weak-signal background scans.\n"
                "# TYPE hvac_filter_wifi_roam_scans_total counter\nhvac_filter_wifi_roam_scans_total %llu\n"
                "# HELP hvac_filter_wifi_roam_attempts_total Stronger-AP reconnect attempts.\n"
                "# TYPE hvac_filter_wifi_roam_attempts_total counter\nhvac_filter_wifi_roam_attempts_total %llu\n"
                "# HELP hvac_filter_wifi_roam_scan_errors_total Aborted or failed roam scans.\n"
                "# TYPE hvac_filter_wifi_roam_scan_errors_total counter\nhvac_filter_wifi_roam_scan_errors_total %llu\n"
                "# HELP hvac_filter_syslog_sent_total UDP events accepted by local network stack, not delivery confirmation.\n"
                "# TYPE hvac_filter_syslog_sent_total counter\nhvac_filter_syslog_sent_total %llu\n"
                "# HELP hvac_filter_syslog_errors_total UDP send or socket failures, events discarded.\n"
                "# TYPE hvac_filter_syslog_errors_total counter\nhvac_filter_syslog_errors_total %llu\n"
                "# HELP hvac_filter_syslog_dropped_total Events overwritten before forwarding.\n"
                "# TYPE hvac_filter_syslog_dropped_total counter\nhvac_filter_syslog_dropped_total %llu\n"
                "# HELP hvac_filter_free_heap_bytes Available heap at latest background health check.\n"
                "# TYPE hvac_filter_free_heap_bytes gauge\nhvac_filter_free_heap_bytes %lu\n"
                "# HELP hvac_filter_config_revision Current configuration revision within this boot.\n"
                "# TYPE hvac_filter_config_revision gauge\nhvac_filter_config_revision %lu\n"
                "# HELP hvac_filter_build_info Firmware identity.\n"
                "# TYPE hvac_filter_build_info gauge\nhvac_filter_build_info{version=\"" DFM_VERSION "\",board=\"xiao-esp32c6\"} 1\n",
                "",r.connected,r.power_save_applied,(unsigned long long)r.reconnects,
                (unsigned long long)r.roam_scans,(unsigned long long)r.roam_attempts,(unsigned long long)r.roam_scan_errors,
                (unsigned long long)r.syslog_sent,(unsigned long long)r.syslog_errors,(unsigned long long)r.syslog_dropped,
                (unsigned long)r.free_heap,(unsigned long)r.revision);
            if(n<0 || (size_t)n>=METRIC_BUFFER-reserve-length) {status="503 Service Unavailable";length=0;}
            else length+=(size_t)n;
            if(r.connected) {
                n=snprintf(body+length,METRIC_BUFFER-reserve-length,"hvac_filter_wifi_rssi_dbm %d\n",r.rssi);
                if(n>0 && (size_t)n<METRIC_BUFFER-reserve-length) length+=(size_t)n;
                else {status="503 Service Unavailable";length=0;}
            }
        }
    } else if(strncmp(client->request,"GET /healthz HTTP/1.",20)==0) {
        content="text/plain";length=(size_t)snprintf(body,METRIC_BUFFER-reserve,"metrics listener ready\n");
    } else {status="404 Not Found";content="text/plain";length=0;}
    char location[256]="";
    if(strncmp(client->request,"GET / HTTP/1.",13)==0 || strncmp(client->request,"GET /console.js HTTP/1.",22)==0 || strncmp(client->request,"GET /api/v1/",12)==0) {
        char host[97]="",path[81]="";const char *line=strstr(client->request,"\r\n");
        while(line && line[2]) {
            line+=2;
            if(!strncasecmp(line,"Host:",5)) {
                const char *start=line+5;while(*start==' ' || *start=='\t')start++;
                size_t n=0;while(start[n] && start[n]!=':' && start[n]!='\r' && n<sizeof(host)-1) {host[n]=start[n];n++;}
                host[n]=0;break;
            }
            line=strstr(line,"\r\n");
        }
        bool safe=host[0]!=0;
        for(size_t i=0;host[i];i++) if(!((host[i]>='a'&&host[i]<='z') || (host[i]>='A'&&host[i]<='Z') || (host[i]>='0'&&host[i]<='9') || strchr("._-",host[i])))safe=false;
        size_t n=0;const char *start=client->request+4;
        while(start[n] && start[n]!=' ' && n<sizeof(path)-1) {path[n]=start[n];n++;}path[n]=0;
        for(size_t i=0;path[i];i++)if(!((path[i]>='a'&&path[i]<='z') || (path[i]>='0'&&path[i]<='9') || strchr("/._-",path[i])))safe=false;
        if(safe) {status="307 Temporary Redirect";snprintf(location,sizeof(location),"Location: http://%s:8080%s\r\n",host,path);}
        else status="400 Bad Request";
        content="text/plain";length=0;
    }
    char header[512];
    int h=snprintf(header,sizeof(header),"HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n%sConnection: close\r\n\r\n",
        status,content,(unsigned)length,location);
    if(h<=0 || (size_t)h>=sizeof(header)) {client->length=0;client->ready=true;return;}
    memmove(client->response+h,body,length);memcpy(client->response,header,(size_t)h);
    client->length=(size_t)h+length;client->ready=true;
}
static void metrics_worker(void *arg) {
    (void)arg;
    for(unsigned i=0;i<METRIC_CLIENTS;i++) clients[i].fd=-1;
    int listener=socket(AF_INET,SOCK_STREAM,IPPROTO_IP);
    if(listener<0) {runtime_log("metrics socket failed");vTaskDelete(NULL);return;}
    int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(DFM_METRICS_PORT),.sin_addr.s_addr=htonl(DFM_LISTEN_ADDRESS)};
    if(bind(listener,(struct sockaddr *)&address,sizeof(address))<0 || listen(listener,4)<0) {
        runtime_log("metrics bind/listen failed errno=%d",errno);close(listener);vTaskDelete(NULL);return;
    }
    fcntl(listener,F_SETFL,O_NONBLOCK);runtime_primary_ready(true);
    runtime_log("priority metrics listener on port 80; four bounded nonblocking clients");
    for(;;) {
        fd_set reads,writes;FD_ZERO(&reads);FD_ZERO(&writes);FD_SET(listener,&reads);int maximum=listener;
        for(unsigned i=0;i<METRIC_CLIENTS;i++) if(clients[i].fd>=0) {
            if(clients[i].ready) FD_SET(clients[i].fd,&writes);else FD_SET(clients[i].fd,&reads);
            if(clients[i].fd>maximum) maximum=clients[i].fd;
        }
        struct timeval timeout={.tv_usec=20000};
        int result=select(maximum+1,&reads,&writes,NULL,&timeout);
        if(result<0) {vTaskDelay(pdMS_TO_TICKS(20));continue;}
        if(FD_ISSET(listener,&reads)) {
            int fd=accept(listener,NULL,NULL);
            if(fd>=0) {
                unsigned slot=0;while(slot<METRIC_CLIENTS && clients[slot].fd>=0) slot++;
                if(slot==METRIC_CLIENTS) {close(fd);runtime_metric_result(true,true,0);}
                else {
                    metric_client_t *c=&clients[slot];c->fd=fd;c->accepted=esp_timer_get_time();
                    c->received=0;c->length=0;c->sent=0;c->ready=false;c->request[0]=0;
                    fcntl(fd,F_SETFL,O_NONBLOCK);
                }
            }
        }
        for(unsigned i=0;i<METRIC_CLIENTS;i++) {
            metric_client_t *c=&clients[i];if(c->fd<0) continue;
            if(esp_timer_get_time()-c->accepted>250000) {close_client(c,true);continue;}
            if(!c->ready && FD_ISSET(c->fd,&reads)) {
                int n=recv(c->fd,c->request+c->received,sizeof(c->request)-1-c->received,0);
                if(n==0 || (n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK)) {close_client(c,true);continue;}
                if(n>0) {
                    c->received+=(size_t)n;c->request[c->received]=0;
                    if(strstr(c->request,"\r\n\r\n")) prepare_response(c);
                    else if(c->received==sizeof(c->request)-1) {close_client(c,true);continue;}
                }
            }
            if(c->ready && FD_ISSET(c->fd,&writes)) {
                int n=send(c->fd,c->response+c->sent,c->length-c->sent,0);
                if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK) {close_client(c,true);continue;}
                if(n>0) c->sent+=(size_t)n;
                if(c->sent==c->length) close_client(c,false);
            }
        }
    }
}
void metrics_start(void) {
    configASSERT(xTaskCreate(metrics_worker,"dfm_metrics",4096,NULL,8,&metrics_task_handle)==pdPASS);
}
