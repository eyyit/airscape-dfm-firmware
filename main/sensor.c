/* SPDX-FileCopyrightText: 2026 the DFM firmware contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "runtime.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include <string.h>

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t device;
static esp_err_t start_measurement(uint32_t timeout_ms) {
    const uint8_t stop[]={0x3f,0xf9}, start[]={0x36,0x15};
    esp_err_t err=i2c_master_transmit(device,stop,sizeof(stop),(int)timeout_ms);
    if(err!=ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(2)); /* Datasheet requires >=500 us after stop. */
    err=i2c_master_transmit(device,start,sizeof(start),(int)timeout_ms);
    if(err==ESP_OK) vTaskDelay(pdMS_TO_TICKS(10));
    return err;
}
static void sensor_worker(void *arg) {
    (void)arg;
    dfm_state_t state={.error=DFM_NOT_READY};
    dfm_config_t config; runtime_config_read(&config);
    i2c_master_bus_config_t bus_config={.i2c_port=I2C_NUM_0,.sda_io_num=22,
        .scl_io_num=23,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,
        .flags.enable_internal_pullup=true};
    esp_err_t err=i2c_new_master_bus(&bus_config,&bus);
    if(err==ESP_OK) {
        i2c_device_config_t dc={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x25,.scl_speed_hz=100000};
        err=i2c_master_bus_add_device(bus,&dc,&device);
    }
    if(err!=ESP_OK) {
        runtime_log("I2C setup failed: %s; sensor unavailable",esp_err_to_name(err));
        for(;;) {dfm_fail(&state,DFM_I2C,esp_timer_get_time());runtime_publish(&state);vTaskDelay(pdMS_TO_TICKS(1000));}
    }
    bool initialized=false;
    int64_t last_recovery=0;
    TickType_t wake=xTaskGetTickCount();
    for(;;) {
        if(runtime_sensor_config_boundary(&config)) {
            state.valid=false;state.filter_seeded=false;
            runtime_log("configuration applied at acquisition boundary; filter reseeded");
        }
        bool reset_filter=false, reset=runtime_take_sensor_action(&reset_filter);
        if(reset_filter) {state.filter_seeded=false;runtime_log("filter state reset (no calibration change)");}
        int64_t began=esp_timer_get_time();
        if(reset || (!initialized && (!last_recovery || began-last_recovery>=(int64_t)config.backoff_ms*1000))) {
            state.valid=false;runtime_publish(&state);
            last_recovery=began;state.recoveries++;
            i2c_master_bus_reset(bus);
            err=start_measurement(config.timeout_ms);
            initialized=err==ESP_OK;
            if(initialized) state.filter_seeded=false;
            runtime_log("sensor initialization/recovery: %s",esp_err_to_name(err));
        }
        uint32_t previous_errors=state.consecutive_errors;
        uint8_t frame[9];
        if(initialized) {
            err=i2c_master_receive(device,frame,sizeof(frame),(int)config.timeout_ms);
            if(err==ESP_OK) dfm_accept(&state,&config,frame,esp_timer_get_time());
            else dfm_fail(&state,DFM_I2C,esp_timer_get_time());
        } else dfm_fail(&state,DFM_I2C,esp_timer_get_time());
        if(previous_errors && !state.consecutive_errors) runtime_log("sensor samples recovered after %lu failures",(unsigned long)previous_errors);
        if(state.consecutive_errors>=config.recovery_failures) initialized=false;
        if(state.consecutive_errors==1) runtime_log("sample failed: error=%d",state.error);
        state.duration_us=(uint32_t)(esp_timer_get_time()-began);
        if(state.duration_us>state.max_duration_us) state.max_duration_us=state.duration_us;
        TickType_t period=pdMS_TO_TICKS(config.interval_ms);
        if(xTaskGetTickCount()-wake>=period) {state.missed_deadlines++;wake=xTaskGetTickCount();}
        runtime_publish(&state);
        vTaskDelayUntil(&wake,period);
    }
}
void sensor_start(void) {
    configASSERT(xTaskCreate(sensor_worker,"dfm_sensor",4096,NULL,10,&sensor_task_handle)==pdPASS);
}
