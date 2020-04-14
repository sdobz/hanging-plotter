#include <stdint.h>
#include "driver/gpio.h"
#include "soc/rmt_caps.h"
#include "soc/rmt_struct.h"
#include "hal/rmt_types.h"

typedef void (*stepper_position_report_t)(long pos);

#define NUM_PINS 4
#define NUM_STEPPERS 2

typedef struct
{
    int32_t steps[NUM_STEPPERS];
    uint16_t duration;
} stepper_task_t;

typedef stepper_task_t *(*stepper_get_task_t)();

void stepper_init(gpio_num_t[NUM_STEPPERS][NUM_PINS], rmt_channel_t[NUM_STEPPERS][NUM_PINS], stepper_position_report_t, stepper_get_task_t);
void stepper_start();

#define SHORTEST_TICK 100 // TODO: Determine this experimentally
#define LONGEST_TICK 10280 // TODO: Calculate this based on hardware defines
