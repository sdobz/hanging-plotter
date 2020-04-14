#include "stepper.h"
#include <stdio.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/periph_ctrl.h"
#include <hal/rmt_ll.h>
#include <hal/rmt_hal.h>

#include <inttypes.h>

#include "debug.h"

#define RMT_BUFFER_SIZE 8  // number of rmt_item32_t to store in buffer, note 2 steps per item
#define RMT_BUFFER_COUNT 2 // 2 framebuffers

#define RMT_DIV 255
#define RMT_HZ APB_CLK_FREQ
#define US_PER_SEC 1000000
#define CYCLES_PER_US (RMT_HZ / US_PER_SEC)

#define US_TO_CYCLES(n) (((n)*RMT_DIV) / CYCLES_PER_US)

#define RMT_SOURCE_CLK(select) ((select == RMT_BASECLK_REF) ? (RMT_SOURCE_CLK_REF) : (RMT_SOUCCE_CLK_APB))

// Spinlock for protecting concurrent register-level access only
static portMUX_TYPE stepper_spinlock = portMUX_INITIALIZER_UNLOCKED;
#define STEPPER_LOCK()  portENTER_CRITICAL_SAFE(&stepper_spinlock)
#define STEPPER_UNLOCK()   portEXIT_CRITICAL_SAFE(&stepper_spinlock)

static uint8_t step_seq[] = {
    0b0001,
    0b0101,
    0b0100,
    0b0110,
    0b0010,
    0b1010,
    0b1000,
    0b1001};

static rmt_item32_t end_marker = {{{0, 0, 0, 0}}};

typedef struct
{
    rmt_channel_t _rmt_channel[NUM_PINS];
    volatile rmt_item32_t *_tx_buf[NUM_PINS];

    // Absolute step counter, may wrap on multiples of sizeof(step_seq)
    int32_t _abs_steps;
    // How many steps left on executing this plan
    uint16_t _steps_remaining;
    // How long to wait between steps for this segment of the plan
    int32_t _ticks_per_step;
    // -1 or 1 depending on which sign the plan says
    int8_t _step_dir;
    // Which fraction of the hardware buffer to write to next
    uint8_t _buffer_segment;
    // A pointer to the stepper_task steps for this stepper
    int32_t *_planned_steps;
} stepper_state_t;

#define DEBUG_STEPPER(stepper) DEBUG_PRINT(      \
    "ch: %d abs: %d pln: %d rem: %d clk: %d dir: %d seg: %d\n", \
    stepper->_rmt_channel[0], \
    stepper->_abs_steps,                         \
    *stepper->_planned_steps, \
    stepper->_steps_remaining,                   \
    stepper->_ticks_per_step,                      \
    stepper->_step_dir,                          \
    stepper->_buffer_segment)

static stepper_state_t stepper_state[NUM_STEPPERS];
static stepper_state_t *stepper_channel[RMT_CHANNEL_MAX];

// Global context to manage stepper
static intr_handle_t gRMT_intr_handle = NULL;
static rmt_hal_context_t hal;
// How many steppers have run out of steps and need a plan
static uint8_t steppers_with_tasks;
static stepper_task_t stepper_task;

static stepper_position_report_t stepper_position_reporter;
// Calls an external system to consume another task
static stepper_get_task_t stepper_get_task;

IRAM_ATTR void stepper_isr(void *arg);
void stepper_init_one(stepper_state_t *stepper, gpio_num_t pins[NUM_PINS], rmt_channel_t channels[NUM_PINS]);


void stepper_init(gpio_num_t pins[NUM_STEPPERS][NUM_PINS], rmt_channel_t channels[NUM_STEPPERS][NUM_PINS], stepper_position_report_t r, stepper_get_task_t step_getter)
{
    stepper_position_reporter = r;
    stepper_get_task = step_getter;
    for (size_t s=0;s<NUM_STEPPERS;s++) {
        stepper_state[s]._planned_steps = &stepper_task.steps[s];
        stepper_init_one(&stepper_state[s], pins[s], channels[s]);
    }
}

void stepper_init_one(stepper_state_t *stepper, gpio_num_t pins[NUM_PINS], rmt_channel_t channels[NUM_PINS])
{
    STEPPER_LOCK();
    periph_module_enable(PERIPH_RMT_MODULE);
    rmt_hal_init(&hal);

    for (size_t i = 0; i < NUM_PINS; i++)
    {
        rmt_channel_t channel = channels[i];
        gpio_num_t gpio_num = pins[i];
        stepper->_rmt_channel[i] = channel;
        stepper_channel[channel] = stepper;

        PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[gpio_num], PIN_FUNC_GPIO);
        gpio_set_direction(gpio_num, GPIO_MODE_OUTPUT);
        gpio_matrix_out(gpio_num, RMT_SIG_OUT0_IDX + channel, 0, 0);

        rmt_ll_set_counter_clock_div(&RMT, channel, RMT_DIV);
        rmt_ll_enable_mem_access(&RMT, true);
        rmt_ll_set_counter_clock_src(&RMT, channel, RMT_BASECLK_APB);
        rmt_ll_set_mem_blocks(&RMT, channel, 1);
        rmt_ll_set_mem_owner(&RMT, channel, RMT_MEM_OWNER_HW);

        rmt_ll_enable_tx_cyclic(&RMT, channel, false);
        rmt_ll_enable_tx_pingpong(&RMT, true);
        /*Set idle level */
        rmt_ll_enable_tx_idle(&RMT, channel, false);
        rmt_ll_set_tx_idle_level(&RMT, channel, 0);
        /*Set carrier*/
        rmt_ll_enable_tx_carrier(&RMT, channel, false);
        rmt_ll_set_carrier_to_level(&RMT, channel, 0);
        rmt_ll_set_carrier_high_low_ticks(&RMT, channel, 0, 0);

        rmt_hal_channel_reset(&hal, channel);

        stepper->_tx_buf[i] = (volatile rmt_item32_t *)&RMTMEM.chan[channel].data32;
        for (int b = 0; b < (RMT_BUFFER_SIZE * RMT_BUFFER_COUNT + 1); b++)
        {
            stepper->_tx_buf[i][b].val = end_marker.val;
        }
    }
    rmt_ll_set_tx_limit(hal.regs, channels[0], RMT_BUFFER_SIZE);
    rmt_ll_enable_tx_thres_interrupt(hal.regs, channels[0], true);

    if (gRMT_intr_handle == NULL)
        esp_intr_alloc(ETS_RMT_INTR_SOURCE, 0, stepper_isr, 0, &gRMT_intr_handle);
    STEPPER_UNLOCK();
}

void stepper_update_task(stepper_state_t *stepper)
{
    STEPPER_LOCK();
    if (steppers_with_tasks == 0) {
        // The first stepper to interrupt fills out the task
        stepper_task = *stepper_get_task();
        // TODO: assert stepper_task is in acceptable error ranges wrt steps vs duration
        steppers_with_tasks = NUM_STEPPERS;
    }
    // The remaining steppers use the plan loaded by the first stepper
    steppers_with_tasks--;
    int32_t planned_steps = *stepper->_planned_steps;

    if (stepper_task.duration != 0) {
        if (planned_steps != 0) {
            stepper->_steps_remaining = abs(planned_steps);
        }
        else  {
            // If steps are 0 the channel still has to wait duration
            // Make up some reasonable number of steps
            // TODO: deal with error in stepper counts
            stepper->_steps_remaining = 100;
        }
        stepper->_ticks_per_step = US_TO_CYCLES(stepper_task.duration/stepper->_steps_remaining);
        if (stepper->_ticks_per_step > LONGEST_TICK) {
            DEBUG_PRINT("ERR: too many steps\n");
            abort();
        }
        stepper->_step_dir = planned_steps > 0 ? 1 : planned_steps < 0 ? -1 : 0;
    }
    STEPPER_UNLOCK();
}

uint8_t stepper_pop_mask(stepper_state_t *stepper)
{
    uint8_t mask = step_seq[stepper->_abs_steps & 0x7];

    if (stepper->_steps_remaining != 0)
    {
        stepper->_abs_steps += stepper->_step_dir;
        stepper->_steps_remaining--;
    }
    else
    {
        stepper_update_task(stepper);
    }

    return mask;
}

void stepper_fill_buffer(stepper_state_t *stepper)
{
    DEBUG_STEPPER(stepper);
    rmt_item32_t src;
    uint8_t mask0;
    uint8_t mask1;
    uint32_t ticks0;
    uint32_t ticks1;
    for (size_t offset = 0; offset < RMT_BUFFER_SIZE; offset++)
    {
        mask0 = stepper_pop_mask(stepper);
        ticks0 = stepper->_ticks_per_step;
        // DEBUG_STEPPER(stepper);
        mask1 = stepper_pop_mask(stepper);
        ticks1 = stepper->_ticks_per_step;
        // DEBUG_STEPPER(stepper);
        for (size_t i = 0; i < NUM_PINS; i++)
        {
            src.level0 = mask0 & (1 << i) ? 0 : 1;
            src.duration0 = ticks0;
            src.level1 = mask1 & (1 << i) ? 0 : 1;
            src.duration1 = ticks1;
            stepper->_tx_buf[i][stepper->_buffer_segment * RMT_BUFFER_SIZE + offset].val = src.val;
        }
        // TODO: manage channels for step/direction interface
    }

    if (stepper->_steps_remaining == 0)
    {
        // pop_mask would have refilled steps_remaining if any remained, the channel is done
        for (size_t i = 0; i < NUM_PINS; i++)
        {
            // Stop looping after this buffer
            rmt_ll_enable_tx_cyclic(hal.regs, stepper->_rmt_channel[i], false);
        }
    }

    // Wrapping increment the buffer
    stepper->_buffer_segment = stepper->_buffer_segment == RMT_BUFFER_COUNT - 1 ? 0 : stepper->_buffer_segment + 1;
}

IRAM_ATTR void stepper_isr(void *arg)
{
    uint32_t status = 0;
    uint8_t channel = 0;

    // Tx end interrupt
    status = rmt_ll_get_tx_end_interrupt_status(hal.regs);
    while (status) {
        channel = __builtin_ffs(status) - 1;
        status &= ~(1 << channel);

        rmt_ll_clear_tx_end_interrupt(hal.regs, channel);
    }

    // Tx thres interrupt
    status = rmt_ll_get_tx_thres_interrupt_status(hal.regs);
    while (status) {
        channel = __builtin_ffs(status) - 1;
        status &= ~(1 << channel);

        stepper_state_t *stepper = stepper_channel[channel];
        if (stepper) {
            stepper_fill_buffer(stepper);
        }

        rmt_ll_clear_tx_thres_interrupt(hal.regs, channel);
    }

    // Err interrupt
    status = rmt_ll_get_err_interrupt_status(hal.regs);
    while (status) {
        channel = __builtin_ffs(status) - 1;
        status &= ~(1 << channel);
        rmt_ll_clear_err_interrupt(hal.regs, channel);
    }
}

void stepper_prep_tx(stepper_state_t *stepper)
{
    stepper->_buffer_segment = 0;
    stepper->_steps_remaining = 0;
    size_t i;
    for (i = 0; i < RMT_BUFFER_COUNT; i++) {
        stepper_fill_buffer(stepper);
    }
    for (i = 0; i < NUM_PINS; i++)
    {
        // Starts transmission
        rmt_ll_reset_tx_pointer(hal.regs, stepper->_rmt_channel[i]);
        rmt_ll_enable_tx_cyclic(hal.regs, stepper->_rmt_channel[i], true);
    }
}

void stepper_start()
{
    STEPPER_LOCK();
    steppers_with_tasks = 0;
    STEPPER_UNLOCK();
    size_t s;
    for (s = 0; s < NUM_STEPPERS; s++)
    {
        stepper_prep_tx(&stepper_state[s]);
    }

    for (s = 0; s < NUM_STEPPERS; s++) {
        for (size_t i = 0; i < NUM_PINS; i++)
        {
            rmt_ll_start_tx(hal.regs, stepper_state[s]._rmt_channel[i]);
        }
    }
}
