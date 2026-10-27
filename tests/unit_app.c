#include "../main/app_main.c"
#include <assert.h>
int main(void){fake_reset();app_main();assert(fake_runtime_started==1 && fake_sensor_started==1 && fake_metrics_started==1 && fake_management_started==1 && fake_network_started==1 && fake_health_started==1);fake_reset();fake_nvs_init_result=ESP_FAIL;app_main();assert(fake_sensor_started==1 && fake_metrics_started==1 && fake_management_started==1);puts("PASS app startup: independent services and delayed health gate");return 0;}
