#define DFM_SYSLOG_HOST
static const char *test_syslog_address="";
#define DFM_SYSLOG_ADDRESS test_syslog_address
#define DFM_SYSLOG_PORT 38514
#include "runtime.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <assert.h>
static bool fail_socket,fail_send,connected;
static uint64_t sent_count,error_count,drop_count;
static dfm_log_t events[8];static size_t event_count;
static int controlled_socket(int a,int b,int c){return fail_socket?-1:socket(a,b,c);}
static ssize_t controlled_send(int fd,const void *p,size_t n,int flags,const struct sockaddr *a,socklen_t l){assert(flags==MSG_DONTWAIT);return fail_send?-1:sendto(fd,p,n,flags,a,l);}
#define socket controlled_socket
#define sendto controlled_send
#include "../main/syslog.c"
#undef socket
#undef sendto
void runtime_snapshot(dfm_state_t *s,dfm_config_t *c,dfm_runtime_t *r){(void)s;(void)c;*r=(dfm_runtime_t){.connected=connected,.boot_id=42};}
void runtime_syslog_result(uint64_t s,uint64_t e,uint64_t d){sent_count+=s;error_count+=e;drop_count+=d;}
size_t runtime_logs_after(uint64_t cursor,dfm_log_t *out,size_t cap,uint64_t *lost){*lost=cursor==0?2:0;size_t n=0;for(size_t i=0;i<event_count && n<cap;i++)if(events[i].sequence>cursor)out[n++]=events[i];return n;}
int main(void){
 fake_reset();syslog_start();assert(fake_task_priority("dfm_syslog")==1);
 int receiver=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);assert(receiver>=0);
 struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(38514),.sin_addr={htonl(INADDR_LOOPBACK)}};
 assert(bind(receiver,(struct sockaddr*)&address,sizeof(address))==0);
 for(int i=0;i<8;i++){events[i]=(dfm_log_t){.sequence=(uint64_t)i+3,.time_us=1230000};strcpy(events[i].message,"safe event\nforged-line");}event_count=8;
 syslog_poll();assert(syslog_cursor==0);connected=true;
 syslog_poll();assert(syslog_cursor==0 && syslog_fd==-1);
 test_syslog_address="127.0.0.1";
 syslog_poll();assert(sent_count==4 && drop_count==2 && syslog_cursor==6);
 char packet[512];int n=recv(receiver,packet,sizeof(packet)-1,MSG_DONTWAIT);assert(n>0);packet[n]=0;
 assert(strstr(packet,"<14>1 - dfm-device.example.invalid airscape-dfm 0000002a EVENT - seq=3 uptime=1.230 safe event forged-line"));
 fail_send=true;syslog_poll();assert(error_count==4 && syslog_fd==-1 && syslog_cursor==10);
 syslog_cursor=6;fail_send=false;fail_socket=true;syslog_poll();assert(error_count==8);
 fail_socket=false;syslog_cursor=6;fake_run(syslog_worker,2);assert(sent_count==8);
 if(syslog_fd>=0){close(syslog_fd);}close(receiver);
 puts("PASS syslog: real UDP packet, bounded drain, offline retention, no retries, loss/socket failure, sanitization and priority");return 0;
}
