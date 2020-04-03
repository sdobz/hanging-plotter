#include <stdint.h>
#include "driver/gpio.h"

typedef void (*stepper_position_report_t)(long pos);

void stepper_init(gpio_num_t[4], stepper_position_report_t);
void stepper_set_velocities(int8_t a_vel, int8_t b_vel);
