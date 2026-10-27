#include "../main/servers.c"
#include <assert.h>
static void initialize(void){fake_reset();json_allocation_failed=false;cJSON_InitHooks(NULL);}
static httpd_req_t req(const char *uri,const char *body){return (httpd_req_t){.uri=uri,.body=(const uint8_t *)body,.content_len=(int)strlen(body),.request_type="application/json"};}
static bool parsed(const char *body){dfm_command_t cmd;cJSON *v=cJSON_Parse(body);bool ok=parse_config(v,&cmd);cJSON_Delete(v);return ok;}
int main(void){
 initialize();
 cJSON *identity=status_json();
 cJSON *logging=cJSON_GetObjectItemCaseSensitive(identity,"syslog");
 assert(!strcmp(cJSON_GetObjectItemCaseSensitive(logging,"hostname")->valuestring,DFM_DEVICE_HOSTNAME));
 cJSON_Delete(identity);
 httpd_req_t r=req("/api/v1/status","");assert(admit(&r));r.authorization="wrong";assert(admit(&r));
 r.request_type="application/json";fake_heap=100;assert(!admit(&r));fake_heap=1000000;
 assert(parsed("{\"revision\":1,\"persist\":false,\"offset_pa\":2}"));
 const char *bad[]={"{\"revision\":1,\"persist\":false,\"unknown\":3}","{\"revision\":1,\"persist\":false,\"alpha\":0}","{\"revision\":1,\"persist\":false,\"revision\":2}","{\"revision\":1.5,\"persist\":false}","{\"revision\":1,\"persist\":true,\"interval_ms\":200}","{\"revision\":1,\"persist\":false,\"power_save\":1}","{\"revision\":1,\"persist\":false,\"timeout_ms\":1000}","{\"revision\":1,\"persist\":false,\"orientation\":0}"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!parsed(bad[i]));
 assert(parsed("{\"revision\":1,\"persist\":false,\"interval_ms\":2000,\"stale_ms\":4000,\"timeout_ms\":200,\"backoff_ms\":2000,\"recovery_failures\":5,\"alpha\":0.5,\"setpoint_pa\":200,\"hysteresis_pa\":15,\"orientation\":-1,\"power_save\":true}"));
 const char *paths[]={"/api/v1/status","/api/v1/history","/api/v1/logs","/api/v1/support"};
 for(unsigned i=0;i<4;i++){r=req(paths[i],"");assert(get_api(&r)==0);cJSON *v=cJSON_Parse(r.reply);assert(v);cJSON_Delete(v);assert(!strstr(r.reply,"aaaaaaaa"));}
 fake_state.have_sample=true;fake_state.valid=true;fake_state.acquired_us=fake_now;fake_state.frame.sensor_pa=1.5f;fake_state.corrected_pa=2.0f;
 r=req("/api/v1/status","");get_api(&r);cJSON *current=cJSON_Parse(r.reply);assert(cJSON_GetObjectItemCaseSensitive(current,"sensor_dp_pa")->valuedouble==1.5);assert(cJSON_GetObjectItemCaseSensitive(current,"processing_difference_pa")->valuedouble==0.5);cJSON_Delete(current);
 fake_state.valid=false;r=req("/api/v1/status","");get_api(&r);current=cJSON_Parse(r.reply);assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(current,"cor_dp_pa")));assert(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(current,"last_good")));cJSON_Delete(current);
 r=req("/api/v1/config","{\"revision\":1,\"persist\":false}");assert(post_api(&r)==0 && !strcmp(r.status,"202 Accepted"));
 const char *actions[]={"sensor_reset","filter_reset","wifi_reconnect","reboot","confirm_trial","debug_logs","unknown"};
 for(unsigned i=0;i<7;i++){char b[128];snprintf(b,sizeof(b),"{\"action\":\"%s\",\"acknowledge_interruption\":true}",actions[i]);r=req("/api/v1/action",b);post_api(&r);assert(!strcmp(r.status,i<5?"202 Accepted":"400 Bad Request"));}
 r=req("/api/v1/config","bad JSON");post_api(&r);assert(!strcmp(r.status,"400 Bad Request"));
 r=req("/api/v1/config","");assert(!read_body(&r));
 r=req("/api/v1/config","{}");r.fail_recv_at=1;r.offset=1;assert(!read_body(&r));
 assert(console_page(&r)==0 && strstr(r.reply,"Filter monitor"));
 assert(console_script(&r)==0 && strstr(r.reply,"freshness"));
 r=req("/api/v1/ota","");assert(ota_api(&r)==0);
 r=req("/api/v1/ota/activate","");assert(ota_api(&r)==0);
 management_start();assert(fake_routes==10);
 fake_ssl_result=ESP_FAIL;management_start();assert(fake_routes==10);
 r=req("/api/v1/config","{}");r.request_type="text/plain";post_api(&r);assert(!strcmp(r.status,"415 Unsupported Media Type"));
 fake_heap=10;json_allocation_failed=false;assert(!diagnostic_alloc(1) && json_allocation_failed);fake_heap=1000000;
 r=req("/api/v1/status","");send_json(&r,NULL);assert(!strcmp(r.status,"503 Service Unavailable"));
 puts("PASS management: no-login HTTP, strict config parsing, exports/redaction, action validation, bounded HTTP startup, allocation reserve");return 0;
}
