/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../main/runtime.c"
#include <assert.h>
static void boundary(void){dfm_config_t c;runtime_sensor_config_boundary(&c);}
static void reset_runtime(void){
 fake_reset();current=(dfm_state_t){.error=DFM_NOT_READY};effective=dfm_defaults();status=(dfm_runtime_t){0};
 history_count=history_head=log_count=log_head=log_sequence=0;config_pending=false;have_persisted=false;
 runtime_init();fake_tick_hook=boundary;
}
static void config_command(float offset,bool save){
 dfm_command_t cmd={.type=CMD_CONFIG,.config=effective,.revision=status.revision,.persist=save};
 cmd.config.offset_pa=offset;assert(runtime_queue(&cmd));fake_run(worker,8);
}
static void action(dfm_command_type_t type){dfm_command_t cmd={.type=type};assert(runtime_queue(&cmd));fake_run(worker,5);}
static void emit(const char *f,...){va_list a;va_start(a,f);capture_log(f,a);va_end(a);}
int main(void){
 reset_runtime();assert(fake_task_priority("dfm_control")==3);runtime_log("safe event");emit("SECRET_TOKEN=%s","do-not-export");
 dfm_log_t out[DFM_LOGS];uint64_t dropped;assert(runtime_logs(out,DFM_LOGS,&dropped)>=1);
 for(size_t i=0;i<runtime_logs(out,DFM_LOGS,&dropped);i++)assert(!strstr(out[i].message,"SECRET_TOKEN") && !strstr(out[i].message,"do-not-export"));
 for(int i=0;i<100;i++){runtime_log("event %d",i);}
 assert(runtime_logs(out,4,&dropped)==4 && dropped>0);
 dfm_log_t batch[4];uint64_t lost;
 assert(runtime_logs_after(0,batch,4,&lost)==4 && lost>0);
 uint64_t cursor=batch[3].sequence;assert(runtime_logs_after(cursor,batch,4,&lost)==4 && lost==0 && batch[0].sequence==cursor+1);
 assert(runtime_logs_after(log_sequence,batch,4,&lost)==0);
 runtime_syslog_result(2,1,3);assert(status.syslog_sent==2 && status.syslog_errors==1 && status.syslog_dropped==3);
 for(int i=0;i<200;i++){dfm_state_t s={.valid=true,.attempted_us=(uint64_t)i};runtime_publish(&s);}
 dfm_history_t h[DFM_HISTORY];assert(runtime_history(h,DFM_HISTORY)==DFM_HISTORY && h[DFM_HISTORY-1].time_us==199);
 dfm_runtime_t n={.connected=true,.rssi=-55,.provisioned=true};strcpy(n.ip,"192.0.2.1");runtime_update_network(&n);
 runtime_primary_ready(true);runtime_management_ready(true);runtime_health();runtime_metric_result(true,true,200);runtime_reject_management();
 dfm_state_t s;dfm_config_t c;dfm_runtime_t v;runtime_snapshot(&s,&c,&v);
 runtime_config_read(&c);assert(dfm_config_equal(&c,&effective));
 assert(v.connected && v.metrics_ready && v.management_ready && v.metrics_rejected==1 && v.metrics_max_us==200 && v.management_rejected==1);
 config_command(2,true);assert(effective.offset_pa==2 && fake_nvs_writes==1);
 config_command(2,true);assert(fake_nvs_writes==1); /* unchanged save never writes */
 fake_nvs_commit_result=ESP_FAIL;config_command(3,true);assert(persisted.offset_pa==2);
 config_command(4,false);assert(effective.offset_pa==4);action(CMD_CONFIRM_TRIAL);
 action(CMD_SENSOR_RESET);bool filter=false;assert(runtime_take_sensor_action(&filter));
 action(CMD_FILTER_RESET);assert(!runtime_take_sensor_action(&filter) && filter);
 action(CMD_WIFI_RECONNECT);assert(fake_start_count>0);
 action(CMD_LOG_LEVEL);
 dfm_command_t bad={.type=CMD_CONFIG,.revision=status.revision,.config=effective};bad.config.orientation=0;
 assert(runtime_queue(&bad));fake_run(worker,4);assert(effective.orientation!=0);
 bad.config=effective;bad.revision=0;assert(runtime_queue(&bad));fake_run(worker,4);
 bad.revision=status.revision;bad.config.power_save=!effective.power_save;bad.persist=true;assert(runtime_queue(&bad));fake_run(worker,4);assert(!effective.power_save);
 bad.persist=false;fake_ps_result=ESP_FAIL;assert(runtime_queue(&bad));fake_run(worker,4);assert(!effective.power_save);
 fake_ps_result=0;dfm_command_t trial={.type=CMD_CONFIG,.revision=status.revision,.config=effective};trial.config.offset_pa=8;assert(runtime_queue(&trial));fake_run(worker,600);assert(effective.offset_pa==4); /* trial expired */
 fake_tick_hook=NULL;assert(!apply_at_boundary(&effective));
 reset_runtime();for(int i=0;i<DFM_CMD_QUEUE;i++){dfm_command_t cmd={.type=CMD_CONFIRM_TRIAL};assert(runtime_queue(&cmd));}
 dfm_command_t extra={0};assert(!runtime_queue(&extra));
 reset_runtime();action(CMD_REBOOT);assert(fake_restart_count==1);
 puts("PASS runtime: coherent snapshots, bounded history/logs, boundary handoff, persistence, rejection, expiry, recovery");return 0;
}
