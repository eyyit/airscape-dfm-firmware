#include "../main/sensor.c"
#include <assert.h>
static void run_sensor(int steps){fake_run(sensor_worker,steps);}
int main(void){
 fake_reset();fake_frame_make(-60,5400,60);sensor_start();assert(fake_task_priority("dfm_sensor")==10);
 run_sensor(12);assert(fake_state.successes>1 && fake_state.frame.sensor_pa==-1 && fake_state.valid);
 assert(fake_i2c_reads>1 && fake_state.recoveries==1);
 fake_reset();fake_frame_make(60,5000,60);fake_i2c_result=ESP_FAIL;run_sensor(30);
 assert(!fake_state.valid && fake_state.errors[0]>0 && fake_state.recoveries>1);
 fake_reset();fake_frame_make(60,5000,60);fake_frame[2]^=1;run_sensor(30);
 assert(!fake_state.valid && fake_state.errors[1]>0 && fake_state.recoveries>1);
 fake_reset();fake_frame_make(60,5000,0);run_sensor(10);assert(fake_state.errors[2]>0);
 fake_reset();fake_i2c_tx_result=ESP_FAIL;run_sensor(12);assert(!fake_state.valid && fake_state.recoveries>1);
 fake_reset();fake_i2c_bus_result=ESP_FAIL;run_sensor(5);assert(!fake_state.valid && fake_state.errors[0]>0);
 puts("PASS sensor: initialization, sign, failures, recovery, bad scale, bus unavailable");return 0;
}
