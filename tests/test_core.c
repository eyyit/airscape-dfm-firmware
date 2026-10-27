/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "dfm_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void word(uint8_t *p,int16_t value) {
    uint16_t u=(uint16_t)value;p[0]=(uint8_t)(u>>8);p[1]=(uint8_t)u;p[2]=dfm_crc(p,2);
}
static void frame(uint8_t *p,int16_t pressure,int16_t temp,int16_t scale) {
    word(p,pressure);word(p+3,temp);word(p+6,scale);
}
int main(int argc,char **argv) {
    (void)argc;(void)argv;
    /* Published Sensirion CRC reference vector. */
    uint8_t vector[]={0xbe,0xef};assert(dfm_crc(vector,2)==0x92);
    uint8_t bytes[9],mask=0;dfm_frame_t decoded;
    frame(bytes,-600,5000,60);assert(dfm_decode(bytes,&decoded,&mask)==DFM_OK);
    assert(mask==7 && decoded.sensor_pa==-10 && decoded.temperature_c==25);
    frame(bytes,0,-1000,60);assert(dfm_decode(bytes,&decoded,&mask)==DFM_OK && decoded.temperature_c==-5);
    for(unsigned i=0;i<3;i++) {
        frame(bytes,100,5000,60);bytes[i*3+2]^=1;
        assert(dfm_decode(bytes,&decoded,&mask)==DFM_CRC && !(mask&(1u<<i)));
    }
    frame(bytes,100,5000,0);assert(dfm_decode(bytes,&decoded,&mask)==DFM_SCALE);
    frame(bytes,100,5000,-60);assert(dfm_decode(bytes,&decoded,&mask)==DFM_SCALE);
    dfm_config_t c=dfm_defaults();assert(dfm_config_valid(&c));
    dfm_config_t invalid=c;invalid.alpha=NAN;assert(!dfm_config_valid(&invalid));
    invalid=c;invalid.timeout_ms=1000;assert(!dfm_config_valid(&invalid));
    invalid=c;invalid.stale_ms=1000;assert(!dfm_config_valid(&invalid));
    invalid=c;invalid.orientation=0;assert(!dfm_config_valid(&invalid));
    dfm_state_t state={0};frame(bytes,0,5000,60);dfm_accept(&state,&c,bytes,1000000);
    frame(bytes,6000,5000,60);dfm_accept(&state,&c,bytes,2000000);
    assert(fabsf(state.filter_pa-20)<0.0001f && state.raw_pa==20);
    c.offset_pa=2.5f;c.orientation=-1;dfm_accept(&state,&c,bytes,3000000);
    assert(state.raw_pa==36 && state.canonical_pa==38.5f && state.corrected_pa==-38.5f);
    assert(dfm_fresh(&state,&c,3000000));assert(!dfm_fresh(&state,&c,7000000));
    char output[8192];size_t length;
    assert(dfm_metrics(output,sizeof(output),&state,&c,3000000,&length));
    assert(strstr(output,"hvac_filter_pressure_pa -38.5\n"));
    assert(strstr(output,"hvac_filter_temperature_celsius 25\n"));
    assert(!dfm_metrics(output,20,&state,&c,3000000,&length) && length==0);
    dfm_fail(&state,DFM_I2C,4000000);
    assert(!state.valid && state.errors[0]==1 && state.acquired_us==3000000);
    assert(dfm_metrics(output,sizeof(output),&state,&c,4000000,&length));
    assert(!strstr(output,"\nhvac_filter_pressure_pa ") && !strstr(output,"\nhvac_filter_temperature_celsius "));
    assert(strstr(output,"hvac_filter_sensor_valid 0\n") && strstr(output,"hvac_filter_sample_age_seconds 1\n"));
    c=dfm_defaults();state=(dfm_state_t){0};frame(bytes,-30,5000,60);dfm_accept(&state,&c,bytes,1000000);
    assert(state.raw_pa==-1); /* Negative half rounds away from zero. */
    state=(dfm_state_t){0};frame(bytes,30000,5000,1);dfm_accept(&state,&c,bytes,1000000);
    assert(state.raw_pa==2500);
    c.alpha=1;state=(dfm_state_t){0};frame(bytes,7500,5000,60);dfm_accept(&state,&c,bytes,1000000);
    assert(state.high_static);frame(bytes,6960,5000,60);dfm_accept(&state,&c,bytes,2000000);assert(state.high_static);
    frame(bytes,6900,5000,60);dfm_accept(&state,&c,bytes,3000000);assert(!state.high_static);
    assert(dfm_metrics(output,sizeof(output),&state,&c,3000000,&length));
    assert(strstr(output,"hvac_filter_production_profile 0\n") && !strstr(output,"\nhvac_filter_pressure_pa "));
    c=dfm_defaults();state=(dfm_state_t){0};frame(bytes,6,5400,60);dfm_accept(&state,&c,bytes,1000000);
    assert(dfm_metrics(output,sizeof(output),&state,&c,1000000,&length));
    if(argc>1) {FILE *f=fopen(argv[1],"w");assert(f);assert(fwrite(output,1,length,f)==length);fclose(f);}
    puts("PASS: CRC, signed decoding, scale rejection, EMA/rounding, offset/orientation, freshness, stale omission, hysteresis and bounds");
    return 0;
}
