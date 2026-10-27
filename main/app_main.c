#include "runtime.h"
#include "ota.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"
#include <stdio.h>

void app_main(void) {
    puts("DFM CUSTOM DEVELOPMENT " DFM_VERSION ": no automatic stock NVS erase or calibration");
    esp_err_t nvs_result=nvs_flash_init_partition("dfm_data");
    /* In particular, never erase stock NVS on a format/version/space error. */
    if(nvs_result!=ESP_OK) printf("Custom NVS unavailable: %s; defaults and no provisioning\n",esp_err_to_name(nvs_result));
    runtime_init();sensor_start();
    network_start();metrics_start();management_start();
    syslog_start();dfm_ota_health_start();
    runtime_log("development build ready; hardware validation still pending");
}
