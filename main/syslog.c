/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "runtime.h"
#include "device_identity.h"
#ifdef DFM_SYSLOG_HOST
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#include "lwip/sockets.h"
#endif
#include <stdio.h>
#include <string.h>
/* No producer-side socket work, allocation, DNS, flash, or retries. */
static int syslog_fd=-1;
static uint64_t syslog_cursor;
static void syslog_poll(void) {
    dfm_runtime_t state;runtime_snapshot(NULL,NULL,&state);
    if(!state.connected || !DFM_SYSLOG_ADDRESS[0]) return; /* bounded RAM ring retains recent offline events */
    dfm_log_t batch[4];uint64_t lost=0,sent=0,errors=0;
    size_t count=runtime_logs_after(syslog_cursor,batch,4,&lost);
    if(!count) return;
    if(syslog_fd<0) syslog_fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    struct sockaddr_in destination={.sin_family=AF_INET,.sin_port=htons(DFM_SYSLOG_PORT)};
    inet_pton(AF_INET,DFM_SYSLOG_ADDRESS,&destination.sin_addr);
    for(size_t i=0;i<count;i++) {
        char text[DFM_LOG_LENGTH];memcpy(text,batch[i].message,sizeof(text));text[sizeof(text)-1]=0;
        for(size_t j=0;text[j];j++) if((unsigned char)text[j]<32 || (unsigned char)text[j]>126)text[j]=' ';
        char packet[384];
        int n=snprintf(packet,sizeof(packet),"<14>1 - " DFM_DEVICE_HOSTNAME " airscape-dfm %08lx EVENT - seq=%llu uptime=%.3f %s",
            (unsigned long)state.boot_id,(unsigned long long)batch[i].sequence,batch[i].time_us/1e6,text);
        if(syslog_fd>=0 && n>0 && n<(int)sizeof(packet) &&
           sendto(syslog_fd,packet,(size_t)n,MSG_DONTWAIT,(struct sockaddr *)&destination,sizeof(destination))==n) sent++;
        else errors++;
        syslog_cursor=batch[i].sequence; /* lossy UDP; never build a retry backlog */
    }
    if(errors && syslog_fd>=0) {close(syslog_fd);syslog_fd=-1;}
    runtime_syslog_result(sent,errors,lost);
}
static void syslog_worker(void *arg) {
    (void)arg;
    for(;;) {syslog_poll();vTaskDelay(pdMS_TO_TICKS(1000));}
}
void syslog_start(void) {
    configASSERT(xTaskCreate(syslog_worker,"dfm_syslog",4096,NULL,1,NULL)==pdPASS);
}
