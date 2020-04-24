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

#define STEPPER_TASK_BUF RMT_BUFFER_SIZE *RMT_BUFFER_COUNT

#define RMT_DIV 255
#define RMT_HZ APB_CLK_FREQ
#define US_PER_SEC 1000000
#define CYCLES_PER_US (RMT_HZ / US_PER_SEC)

#define US_TO_CYCLES(n) (((n)*RMT_DIV) / CYCLES_PER_US)

#define RMT_SOURCE_CLK(select) ((select == RMT_BASECLK_REF) ? (RMT_SOURCE_CLK_REF) : (RMT_SOUCCE_CLK_APB))

#define STEPPER_TASK(stepper) stepper_tasks[stepper->_task_index]
#define STEPS_REMAINING(stepper) STEPPER_TASK(stepper).steps[stepper->_index]

// Spinlock for protecting concurrent register-level access only
static portMUX_TYPE stepper_spinlock = portMUX_INITIALIZER_UNLOCKED;
#define STEPPER_LOCK() portENTER_CRITICAL_SAFE(&stepper_spinlock)
#define STEPPER_UNLOCK() portEXIT_CRITICAL_SAFE(&stepper_spinlock)

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
static stepper_task_t end_task = {{0}, 0};

typedef struct
{
    rmt_channel_t _rmt_channel[NUM_PINS];
    volatile rmt_item32_t *_tx_buf[NUM_PINS];

    // Absolute step counter, may wrap on multiples of sizeof(step_seq)
    int32_t _abs_steps;

    // How long to wait between steps for this task
    int32_t _ticks_per_step;
    // -1 or 1 depending on the task
    int8_t _step_dir;
    uint16_t _steps_remaining;
    // Idea: step multiplier. Is small number, 0, 1, 2 etc
    // While ticks_per_step is too high the multiplier is incremented
    // ticks per step is shifted down by the multiplier
    // steps remaining is shifted up by the multiplier
    // a step is only taken if steps_remaining & (? << multiplier)
    // Issue: how to ensure exactitude?
    // Steps remaining could be "1" for a 0 step

    // Which fraction of the hardware buffer to write to next
    uint8_t _buffer_segment;
    uint8_t _index;
    uint8_t _task_index;

#if defined(DEBUG) && DEBUG > 0
    char _debug_buffers[RMT_BUFFER_COUNT][RMT_BUFFER_SIZE * 2 + 1];
#endif
} stepper_state_t;
/*
#define DEBUG_STEPPER(stepper) DEBUG_PRINT(             \
    "ch: %d abs: %d rem: %d clk: %d dir: %d seg: %d\n", \
    stepper->_rmt_channel[0],                           \
    stepper->_abs_steps,                                \
    STEPS_REMAINING(stepper),                           \
    stepper->_ticks_per_step,                           \
    stepper->_step_dir,                                 \
    stepper->_buffer_segment)
*/

static stepper_state_t stepper_state[NUM_STEPPERS];
static stepper_state_t *stepper_channel[RMT_CHANNEL_MAX];

// Global context to manage stepper
static intr_handle_t gRMT_intr_handle = NULL;
static rmt_hal_context_t hal;
// How many steppers have run out of steps and need a plan
// TODO: How to determine the maximum size of this buffer?
static stepper_task_t stepper_tasks[STEPPER_TASK_BUF];
static uint8_t stepper_task_active[STEPPER_TASK_BUF];

static stepper_position_report_t stepper_position_reporter;
// Calls an external system to consume another task
static stepper_get_task_t stepper_get_task;

#if defined(DEBUG) && DEBUG > 0

static char step_dir_debug[] = {'v', '-', '^'};

char stepper_task_char(stepper_state_t *stepper, uint8_t task_index)
{
    if (stepper->_task_index == task_index)
    {
        return '*';
    }

    stepper_task_t *task = &stepper_tasks[task_index];
    if (task->duration == 0)
    {
        return ' ';
    }

    bool me;
    bool them;
    for (size_t s = 0; s < NUM_STEPPERS; s++)
    {
        if (task->steps[s] != 0)
        {
            if (&stepper_state[s] == stepper)
            {
                me = true;
            }
            else
            {
                them = true;
            }
        }
    }

    if (me && them)
    {
        return '%';
    }
    else if (me)
    {
        return 'o';
    }
    else if (them)
    {
        return 'x';
    }
    return '0';
}

void stepper_debug(stepper_state_t *stepper)
{
    // Task legend:
    // % - both steppers
    // x - other stepper
    // * - current stepper
    //   - duration 0
    // 0 - duration and 0 steps
    // ! - duration 0 with steps

    /*
[0]    123 ^ ( 1233) [ /- ]> \| < [   %xxx    ]
*/
    DEBUG_PRINT("[%d] %6d %3d%c (%5d)",
                stepper->_index,
                stepper->_abs_steps,
                STEPS_REMAINING(stepper),
                step_dir_debug[stepper->_step_dir + 1],
                stepper->_ticks_per_step);

    for (size_t b = 0; b < RMT_BUFFER_COUNT; b++)
    {
        DEBUG_PRINT(" %c %s %c",
                    b == stepper->_buffer_segment ? '>' : '[',
                    stepper->_debug_buffers[b],
                    b == stepper->_buffer_segment ? '<' : ']');
    }
    DEBUG_PRINT(" { ");
    for (size_t t = 0; t < STEPPER_TASK_BUF; t++)
    {
        DEBUG_PRINT("%c", stepper_task_char(stepper, t));
    }
    DEBUG_PRINT(" }\n");
}

#define DEBUG_STEPPER(stepper) stepper_debug(stepper)
#else
#define DEBUG_STEPPER(stepper)
#endif

IRAM_ATTR void stepper_isr(void *arg);
void stepper_init_one(stepper_state_t *stepper, gpio_num_t pins[NUM_PINS], rmt_channel_t channels[NUM_PINS]);

void stepper_init(gpio_num_t pins[NUM_STEPPERS][NUM_PINS], rmt_channel_t channels[NUM_STEPPERS][NUM_PINS], stepper_position_report_t r, stepper_get_task_t step_getter)
{
    stepper_position_reporter = r;
    stepper_get_task = step_getter;
    for (size_t s = 0; s < NUM_STEPPERS; s++)
    {
        stepper_state[s]._index = s;
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

// Updates stepper state for tasks
void stepper_setup_task(stepper_state_t *stepper)
{
    if (STEPPER_TASK(stepper).duration == 0)
    {
        return;
    }

    // Mark task active for this stepper
    stepper_task_active[stepper->_task_index] |= (1 << stepper->_index);
    if (STEPS_REMAINING(stepper) == 0)
    {
        STEPS_REMAINING(stepper) = 100;
        stepper->_step_dir = 0;
    }
    else
    {
        stepper->_step_dir = STEPS_REMAINING(stepper) > 0 ? 1 : -1;
        STEPS_REMAINING(stepper) = abs(STEPS_REMAINING(stepper));
    }
    stepper->_ticks_per_step = US_TO_CYCLES(STEPPER_TASK(stepper).duration / STEPS_REMAINING(stepper));
    if (stepper->_ticks_per_step > LONGEST_TICK)
    {
        //DEBUG_PRINT("ERR: step too long\n");
        stepper->_ticks_per_step = LONGEST_TICK;
        //abort();
    }
    else if (stepper->_ticks_per_step < SHORTEST_TICK)
    {
        //DEBUG_PRINT("ERR: step too short\n");
        stepper->_ticks_per_step = SHORTEST_TICK;
        //abort();
    }
}

void stepper_update_task(stepper_state_t *stepper)
{
    STEPPER_LOCK();

    // Mark task complete for this stepper
    stepper_task_active[stepper->_task_index] &= ~(1 << stepper->_index);

    if (!stepper_task_active[stepper->_task_index])
    {
        // Clear the old task
        STEPPER_TASK(stepper) = end_task;
    }

    // Looping increment to the next step
    stepper->_task_index = stepper->_task_index == STEPPER_TASK_BUF - 1 ? 0 : stepper->_task_index + 1;

    if (!stepper_task_active[stepper->_task_index])
    {
        DEBUG_PRINT("Stepper %d: Getting new task\n", stepper->_index);
        STEPPER_TASK(stepper) = *stepper_get_task();
    }

    stepper_setup_task(stepper);
    STEPPER_UNLOCK();
}

uint8_t stepper_pop_mask(stepper_state_t *stepper)
{
    uint8_t mask = step_seq[stepper->_abs_steps & 0x7];
    if (STEPS_REMAINING(stepper) == 0)
    {
        return mask;
    }

    stepper->_abs_steps += stepper->_step_dir;
    STEPS_REMAINING(stepper)
    --;

    if (STEPS_REMAINING(stepper) == 0)
    {
        stepper_update_task(stepper);
    }

    return mask;
}

void stepper_fill_buffer(stepper_state_t *stepper)
{
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
#if defined(DEBUG) && DEBUG > 0
        stepper->_debug_buffers[stepper->_buffer_segment][offset * 2] = (stepper->_abs_steps & 0x7) + 48;
#endif
        mask1 = stepper_pop_mask(stepper);
        ticks1 = stepper->_ticks_per_step;
#if defined(DEBUG) && DEBUG > 0
        stepper->_debug_buffers[stepper->_buffer_segment][offset * 2 + 1] = (stepper->_abs_steps & 0x7) + 48;
#endif
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

    if (STEPPER_TASK(stepper).duration == 0)
    {
        DEBUG_PRINT("Stepper %d stopping after this round\n", stepper->_index);
        for (size_t i = 0; i < NUM_PINS; i++)
        {
            // Stop looping after this buffer
            rmt_ll_enable_tx_cyclic(hal.regs, stepper->_rmt_channel[i], false);
        }
    }

    DEBUG_STEPPER(stepper);
    // Wrapping increment the buffer
    stepper->_buffer_segment = stepper->_buffer_segment == RMT_BUFFER_COUNT - 1 ? 0 : stepper->_buffer_segment + 1;
}

IRAM_ATTR void stepper_isr(void *arg)
{
    uint32_t status = 0;
    uint8_t channel = 0;

    // Tx end interrupt
    status = rmt_ll_get_tx_end_interrupt_status(hal.regs);
    while (status)
    {
        channel = __builtin_ffs(status) - 1;
        status &= ~(1 << channel);

        rmt_ll_clear_tx_end_interrupt(hal.regs, channel);
    }

    // Tx thres interrupt
    status = rmt_ll_get_tx_thres_interrupt_status(hal.regs);
    while (status)
    {
        channel = __builtin_ffs(status) - 1;
        status &= ~(1 << channel);

        //DEBUG_PRINT("Interrupt! CH: %d\n", channel);
        stepper_state_t *stepper = stepper_channel[channel];
        if (stepper)
        {
            stepper_fill_buffer(stepper);
        }

        rmt_ll_clear_tx_thres_interrupt(hal.regs, channel);
    }

    // Err interrupt
    status = rmt_ll_get_err_interrupt_status(hal.regs);
    while (status)
    {
        channel = __builtin_ffs(status) - 1;
        status &= ~(1 << channel);
        rmt_ll_clear_err_interrupt(hal.regs, channel);
    }
}

void stepper_prep_tx(stepper_state_t *stepper)
{
    DEBUG_PRINT("Stepper prepping: %d\n", stepper->_index);

    stepper->_task_index = 0;
    stepper->_buffer_segment = 0;
    stepper_setup_task(stepper);
    for (size_t i = 0; i < RMT_BUFFER_COUNT; i++)
    {
#if defined(DEBUG) && DEBUG > 0
        stepper->_debug_buffers[i][RMT_BUFFER_SIZE * 2 + 1] = '\0';
#endif
        stepper_fill_buffer(stepper);
    }
}

void stepper_start()
{
    for (size_t t = 0; t < STEPPER_TASK_BUF; t++)
    {
        stepper_tasks[t] = end_task;
    }
    stepper_tasks[0] = *stepper_get_task();

    size_t s;
    for (s = 0; s < NUM_STEPPERS; s++)
    {
        stepper_prep_tx(&stepper_state[s]);
    }

    stepper_state_t *stepper;
    for (s = 0; s < NUM_STEPPERS; s++)
    {
        stepper = &stepper_state[s];
        for (size_t i = 0; i < NUM_PINS; i++)
        {
            // Starts transmission
            rmt_ll_reset_tx_pointer(hal.regs, stepper->_rmt_channel[i]);
            rmt_ll_enable_tx_cyclic(hal.regs, stepper->_rmt_channel[i], true);
            rmt_ll_start_tx(hal.regs, stepper->_rmt_channel[i]);
        }
    }
}
