/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "../main/ota.c"
#include <assert.h>
static uint8_t package[8192],tiny[2048],wrong[8192];static size_t package_len,tiny_len,wrong_len;
static char public_key[2048];
static size_t load(const char *root,const char *name,void *buffer,size_t capacity){char path[512];snprintf(path,sizeof(path),"%s/%s",root,name);FILE *f=fopen(path,"rb");assert(f);size_t n=fread(buffer,1,capacity,f);assert(feof(f));fclose(f);return n;}
static void reset_ota(void){fake_reset();status=(dfm_ota_status_t){0};staged_partition=NULL;fake_set_text("dfm_provision","ota_pubkey",public_key);}
static httpd_req_t request(uint8_t *p,size_t n){return (httpd_req_t){.uri="/api/v1/ota",.body=p,.content_len=(int)n,.confirmation="accept"};}
static void flash_fails(void){fake_ota_result=ESP_FAIL;}
int main(int argc,char **argv){
 assert(argc==2);package_len=load(argv[1],"image.dfmota",package,sizeof(package));tiny_len=load(argv[1],"tiny.dfmota",tiny,sizeof(tiny));wrong_len=load(argv[1],"wrong-board.dfmota",wrong,sizeof(wrong));size_t n=load(argv[1],"test-only-public.pem",public_key,sizeof(public_key)-1);public_key[n]=0;
 reset_ota();dfm_ota_status_t out;dfm_ota_status(&out);assert(out.enabled && !out.pending_verify);
 httpd_req_t req=request(package,package_len);dfm_ota_upload(&req);assert(status.staged && !status.busy && fake_ota_begin_count==1 && fake_ota_written==4096 && !fake_ota_selected);
 req=request(NULL,0);dfm_ota_activate(&req);assert(status.selected && fake_ota_selected==1);
 reset_ota();req=request(package,package_len);req.confirmation=NULL;dfm_ota_upload(&req);assert(!fake_ota_begin_count && !status.staged);
 reset_ota();package[20]^=1;req=request(package,package_len);dfm_ota_upload(&req);assert(!fake_ota_begin_count);package[20]^=1;
 reset_ota();req=request(wrong,wrong_len);dfm_ota_upload(&req);assert(!fake_ota_begin_count);
 reset_ota();req=request(package,package_len);fake_partition.size=100;dfm_ota_upload(&req);assert(!fake_ota_begin_count);
 reset_ota();package[package_len-1]^=1;req=request(package,package_len);dfm_ota_upload(&req);assert(fake_ota_abort_count==1 && !fake_ota_selected && !status.staged);package[package_len-1]^=1;
 reset_ota();req=request(package,package_len);req.fail_recv_at=16+(int)big32(package+8)+(int)big32(package+12)+288;dfm_ota_upload(&req);assert(fake_ota_abort_count==1);
 reset_ota();req=request(package,package_len);fake_ota_result=ESP_FAIL;dfm_ota_upload(&req);assert(fake_ota_begin_count==1 && !status.staged);
 reset_ota();req=request(package,package_len);fake_tick_hook=flash_fails;dfm_ota_upload(&req);assert(fake_ota_abort_count==1);
 reset_ota();req=request(tiny,tiny_len);fake_tick_hook=flash_fails;dfm_ota_upload(&req);assert(fake_ota_end_count==1 && !status.staged);
 reset_ota();req=request(NULL,0);dfm_ota_activate(&req);assert(!fake_ota_selected);
 reset_ota();req=request(package,10);dfm_ota_upload(&req);assert(!fake_ota_begin_count);
 reset_ota();fake_set_text("dfm_provision","ota_pubkey","invalid public key");dfm_ota_status(&out);assert(!out.enabled);
 fake_reset();dfm_ota_status(&out);assert(!out.enabled);
 reset_ota();cJSON *m=cJSON_Parse("{\"board\":\"xiao-esp32c6\",\"board\":\"xiao-esp32c6\"}");size_t size;char hash[65],elf[65],ver[32];assert(!manifest_valid(m,&size,hash,elf,ver));cJSON_Delete(m);
 reset_ota();fake_ota_state=ESP_OTA_IMG_PENDING_VERIFY;fake_state=(dfm_state_t){.valid=true,.have_sample=true,.successes=3,.acquired_us=(uint64_t)fake_now};dfm_ota_health_start();assert(fake_task_priority("dfm_ota_health")==3);fake_task_run("dfm_ota_health",10);assert(fake_ota_validated==1 && !fake_ota_rollback);
 reset_ota();fake_ota_state=ESP_OTA_IMG_PENDING_VERIFY;fake_run(health_worker,200);assert(fake_ota_rollback==1 && !fake_ota_validated);
 puts("PASS OTA: real signatures, wrong board/key, corruption/interruption, flash errors, explicit activation, delayed health and rollback");return 0;
}
