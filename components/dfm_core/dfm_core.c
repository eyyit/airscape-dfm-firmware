#include "dfm_core.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

dfm_config_t dfm_defaults(void) {
    return (dfm_config_t){.schema=1,.interval_ms=1000,.timeout_ms=100,
        .recovery_failures=3,.backoff_ms=5000,.stale_ms=3000,.alpha=0.2f,
        .offset_pa=0,.setpoint_pa=125,.hysteresis_pa=10,.orientation=1};
}
bool dfm_config_valid(const dfm_config_t *c) {
    return c && c->schema==1 && c->interval_ms>=100 && c->interval_ms<=10000 &&
        c->timeout_ms>=10 && c->timeout_ms<=500 && c->timeout_ms<c->interval_ms &&
        c->recovery_failures>=1 && c->recovery_failures<=20 &&
        c->backoff_ms>=1000 && c->backoff_ms<=60000 &&
        c->stale_ms>=2*c->interval_ms && c->stale_ms<=60000 &&
        isfinite(c->alpha) && c->alpha>0 && c->alpha<=1 &&
        isfinite(c->offset_pa) && fabsf(c->offset_pa)<=500 &&
        isfinite(c->setpoint_pa) && c->setpoint_pa>=25 && c->setpoint_pa<=500 &&
        isfinite(c->hysteresis_pa) && c->hysteresis_pa>=0 &&
        c->hysteresis_pa<c->setpoint_pa && (c->orientation==1 || c->orientation==-1);
}
uint8_t dfm_crc(const uint8_t *data, size_t length) {
    uint8_t crc=0xff;
    for (size_t i=0;i<length;i++) {
        crc^=data[i];
        for (unsigned bit=0;bit<8;bit++) crc=(uint8_t)((crc<<1)^((crc&0x80)?0x31:0));
    }
    return crc;
}
bool dfm_config_equal(const dfm_config_t *a,const dfm_config_t *b) {
    return a->schema==b->schema && a->interval_ms==b->interval_ms &&
        a->timeout_ms==b->timeout_ms && a->recovery_failures==b->recovery_failures &&
        a->backoff_ms==b->backoff_ms && a->stale_ms==b->stale_ms &&
        a->alpha==b->alpha && a->offset_pa==b->offset_pa &&
        a->setpoint_pa==b->setpoint_pa && a->hysteresis_pa==b->hysteresis_pa &&
        a->orientation==b->orientation && a->power_save==b->power_save;
}
static int16_t signed_word(const uint8_t *bytes) {
    uint16_t u=(uint16_t)(((uint16_t)bytes[0]<<8)|bytes[1]);
    return (int16_t)(u>=32768 ? (int32_t)u-65536 : u);
}
dfm_error_t dfm_decode(const uint8_t bytes[9], dfm_frame_t *f, uint8_t *mask) {
    *mask=0;
    for (unsigned i=0;i<3;i++) if (dfm_crc(bytes+3*i,2)==bytes[3*i+2]) *mask|=1u<<i;
    if (*mask!=7) return DFM_CRC;
    int16_t scale=signed_word(bytes+6);
    if (scale<=0) return DFM_SCALE;
    *f=(dfm_frame_t){.counts=signed_word(bytes),.temperature_counts=signed_word(bytes+3),
        .scale=scale,.sensor_pa=(float)signed_word(bytes)/scale,
        .temperature_c=(float)signed_word(bytes+3)/200};
    return DFM_OK;
}
void dfm_fail(dfm_state_t *s, dfm_error_t error, uint64_t now) {
    s->valid=false; s->error=error; s->attempted_us=now; s->consecutive_errors++;
    if (error>=DFM_I2C && error<=DFM_SCALE) s->errors[error-1]++;
}
void dfm_accept(dfm_state_t *s, const dfm_config_t *c, const uint8_t bytes[9], uint64_t now) {
    dfm_frame_t f;
    memcpy(s->last_frame,bytes,9);
    dfm_error_t error=dfm_decode(bytes,&f,&s->crc_valid_mask);
    if (error!=DFM_OK) { dfm_fail(s,error,now); return; }
    float x=fminf(2500,fmaxf(-2500,f.sensor_pa));
    s->filter_pa=s->filter_seeded ? c->alpha*x+(1-c->alpha)*s->filter_pa : x;
    s->filter_pa=fminf(2500,fmaxf(-2500,s->filter_pa));
    s->filter_seeded=true;
    s->raw_pa=roundf(s->filter_pa);
    s->canonical_pa=s->raw_pa+c->offset_pa;
    s->corrected_pa=s->canonical_pa*c->orientation;
    float alarm_pa=roundf(s->corrected_pa);
    if (!s->high_static && alarm_pa>=c->setpoint_pa) s->high_static=true;
    else if (s->high_static && alarm_pa<=c->setpoint_pa-c->hysteresis_pa) s->high_static=false;
    s->frame=f; s->have_sample=true; s->valid=true; s->error=DFM_OK;
    s->acquired_us=now; s->attempted_us=now; s->successes++; s->consecutive_errors=0;
}
bool dfm_fresh(const dfm_state_t *s, const dfm_config_t *c, uint64_t now) {
    return s->valid && s->have_sample && now>=s->acquired_us &&
        now-s->acquired_us<=(uint64_t)c->stale_ms*1000;
}
typedef struct { char *data; size_t capacity, used; bool ok; } writer_t;
static void append(writer_t *w, const char *format, ...) {
    if (!w->ok) return;
    va_list args; va_start(args,format);
    int n=vsnprintf(w->data+w->used,w->capacity-w->used,format,args);
    va_end(args);
    if (n<0 || (size_t)n>=w->capacity-w->used) { w->ok=false; return; }
    w->used+=(size_t)n;
}
static void gauge(writer_t *w,const char *name,const char *help,double value) {
    append(w,"# HELP hvac_filter_%s %s\n# TYPE hvac_filter_%s gauge\nhvac_filter_%s %.8g\n",
        name,help,name,name,value);
}
bool dfm_metrics(char *buffer,size_t cap,const dfm_state_t *s,const dfm_config_t *c,
                 uint64_t now,size_t *length) {
    writer_t w={buffer,cap,0,cap>0};
    bool fresh=dfm_fresh(s,c,now);
    bool baseline=c->interval_ms==1000 && fabsf(c->alpha-0.2f)<0.000001f;
    gauge(&w,"production_profile","Stock-compatible cadence and smoothing are active (0/1).",baseline);
    gauge(&w,"sensor_valid","Latest sensor sample is valid and fresh (0/1).",fresh);
    gauge(&w,"temperature_valid","Internal sensor temperature is valid and fresh (0/1).",fresh);
    gauge(&w,"uptime_seconds","Monotonic uptime in seconds.",(double)now/1e6);
    if (s->have_sample) gauge(&w,"sample_age_seconds","Age of last good acquisition.",
        now>=s->acquired_us ? (double)(now-s->acquired_us)/1e6 : 0);
    gauge(&w,"setpoint_pa","Local high-static threshold; not a filter-change recommendation.",c->setpoint_pa);
    gauge(&w,"hysteresis_pa","Local high-static hysteresis.",c->hysteresis_pa);
    gauge(&w,"calibration_adjustment_pa","Configured additive pressure offset.",c->offset_pa);
    if (fresh) {
        if(baseline) {
        gauge(&w,"pressure_pa","Corrected pressure after smoothing, offset and orientation.",s->corrected_pa);
        gauge(&w,"raw_pressure_pa","Stock-compatible smoothed rounded pressure before offset.",s->raw_pa);
        gauge(&w,"canonical_pressure_pa","Smoothed rounded pressure plus offset, before orientation.",s->canonical_pa);
        }
        gauge(&w,"sensor_pressure_pa","Unsmoothed signed sensor pressure quotient.",s->frame.sensor_pa);
        gauge(&w,"temperature_celsius","Internal sensor temperature, not room temperature.",s->frame.temperature_c);
        gauge(&w,"high_static","Local high-static state, not filter loading (0/1).",s->high_static);
    }
    append(&w,"# HELP hvac_filter_sensor_errors_total Sensor frame or transport errors.\n"
        "# TYPE hvac_filter_sensor_errors_total counter\n");
    const char *reasons[]={"i2c","crc","scale"};
    for (unsigned i=0;i<3;i++) append(&w,"hvac_filter_sensor_errors_total{reason=\"%s\"} %llu\n",
        reasons[i],(unsigned long long)s->errors[i]);
    append(&w,"# HELP hvac_filter_sensor_reinitializations_total Sensor recovery attempts.\n"
        "# TYPE hvac_filter_sensor_reinitializations_total counter\n"
        "hvac_filter_sensor_reinitializations_total %llu\n",(unsigned long long)s->recoveries);
    append(&w,"# HELP hvac_filter_sensor_missed_deadlines_total Acquisition deadlines missed.\n"
        "# TYPE hvac_filter_sensor_missed_deadlines_total counter\n"
        "hvac_filter_sensor_missed_deadlines_total %llu\n",(unsigned long long)s->missed_deadlines);
    gauge(&w,"sample_duration_seconds","Last acquisition transaction duration.",s->duration_us/1e6);
    *length=w.ok?w.used:0;
    return w.ok;
}
