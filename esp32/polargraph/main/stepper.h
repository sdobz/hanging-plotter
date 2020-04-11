#include <stdint.h>
#include "driver/gpio.h"
#include "driver/rmt.h"

typedef void (*stepper_position_report_t)(long pos);
// Function returning an array of two int16_ts representing [steps, us per step]
typedef int16_t (*(*stepper_get_steps_t)(void))[2];

void stepper_init(gpio_num_t[4], rmt_channel_t[4], stepper_position_report_t, stepper_get_steps_t);
void stepper_start();
