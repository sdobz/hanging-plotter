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

#define NUM_PINS 4
#define NUM_STEPPERS 1
#define RMT_BUFFER_SIZE 2  // number of rmt_item32_t to store in buffer, note 2 steps per item
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

// Array of items to transmit to a stepper control pin
typedef rmt_item32_t stepper_rmt_buffer_t[RMT_BUFFER_SIZE];

typedef struct
{
    rmt_channel_t _rmt_channel[NUM_PINS];
    rmt_hal_context_t _hal;
    volatile rmt_item32_t *_tx_buf[NUM_PINS];

    int32_t _abs_steps;
    int32_t _steps_remaining;
    int32_t _clk_per_step;
    int8_t _step_dir;
    uint8_t _buffer_segment;
} stepper_state_t;

#define DEBUG_STEPPER(stepper) DEBUG_PRINT(      \
    "abs: %d rem: %d clk: %d dir: %d seg: %d\n", \
    stepper->_abs_steps,                         \
    stepper->_steps_remaining,                   \
    stepper->_clk_per_step,                      \
    stepper->_step_dir,                          \
    stepper->_buffer_segment)

static stepper_state_t stepper_state[NUM_STEPPERS];

static intr_handle_t gRMT_intr_handle = NULL;

static stepper_position_report_t stepper_position_reporter;
static stepper_get_steps_t stepper_get_steps;

IRAM_ATTR void stepper_isr(void *arg);

void stepper_init(gpio_num_t pins_a[NUM_PINS], rmt_channel_t channels_a[NUM_PINS], stepper_position_report_t r, stepper_get_steps_t step_getter)
{
    stepper_position_reporter = r;
    stepper_get_steps = step_getter;

    stepper_state_t *stepper = &stepper_state[0];

    STEPPER_LOCK();
    periph_module_enable(PERIPH_RMT_MODULE);
    rmt_hal_init(&stepper->_hal);

    for (size_t i = 0; i < NUM_PINS; i++)
    {
        rmt_channel_t channel = channels_a[i];
        gpio_num_t gpio_num = pins_a[i];
        stepper->_rmt_channel[i] = channel;
        //rmt_config_t config = RMT_DEFAULT_CONFIG_TX(pins_a[i], channel);
        //config.tx_config.loop_en = false;
        //config.clk_div = RMT_DIV;

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

        rmt_hal_channel_reset(&stepper->_hal, channel);

        stepper->_tx_buf[i] = (volatile rmt_item32_t *)&RMTMEM.chan[channel].data32;
        for (int b = 0; b < (RMT_BUFFER_SIZE * RMT_BUFFER_COUNT + 1); b++)
        {
            stepper->_tx_buf[i][b].val = end_marker.val;
        }
    }
    rmt_ll_set_tx_limit(stepper->_hal.regs, channels_a[0], RMT_BUFFER_SIZE);
    rmt_ll_enable_tx_thres_interrupt(stepper->_hal.regs, channels_a[0], true);

    if (gRMT_intr_handle == NULL)
        esp_intr_alloc(ETS_RMT_INTR_SOURCE, 0, stepper_isr, 0, &gRMT_intr_handle);
    STEPPER_UNLOCK();
}

void stepper_load_next_steps(stepper_state_t *stepper)
{
    int32_t *step_info = stepper_get_steps();
    int32_t steps_to_take = step_info[0];
    int32_t us_per_step = step_info[1];
    if (steps_to_take == 0)
    {
        // let _steps_remaining stay at 0 for all future pop_masks
        return;
    }
    stepper->_steps_remaining = abs(steps_to_take);
    stepper->_clk_per_step = US_TO_CYCLES(us_per_step);
    stepper->_step_dir = steps_to_take > 0 ? 1 : -1;
}

uint8_t stepper_pop_mask(stepper_state_t *stepper)
{
    if (stepper->_steps_remaining == 0)
    {
        return 0;
    }
    uint8_t mask = step_seq[stepper->_abs_steps & 0x7];
    stepper->_abs_steps += stepper->_step_dir;
    stepper->_steps_remaining--;
    if (stepper->_steps_remaining == 0)
    {
        stepper_load_next_steps(stepper);
    }

    return mask;
}

void stepper_fill_buffer(stepper_state_t *stepper)
{
    rmt_item32_t src = end_marker;
    uint8_t mask0;
    uint8_t mask1;
    uint32_t ticks0;
    uint32_t ticks1;
    for (size_t offset = 0; offset < RMT_BUFFER_SIZE; offset++)
    {
        mask0 = stepper_pop_mask(stepper);
        ticks0 = stepper->_clk_per_step;
        // DEBUG_STEPPER(stepper);
        mask1 = stepper_pop_mask(stepper);
        ticks1 = stepper->_clk_per_step;
        // DEBUG_STEPPER(stepper);
        for (size_t i = 0; i < NUM_PINS; i++)
        {
            src.level0 = mask0 & (1 << i) ? 0 : 1;
            src.duration0 = ticks0;
            src.level1 = mask1 & (1 << i) ? 0 : 1;
            src.duration1 = ticks1;
            stepper->_tx_buf[i][stepper->_buffer_segment * RMT_BUFFER_SIZE + offset].val = src.val;
        }
    }

    if (stepper->_steps_remaining == 0)
    {
        for (size_t i = 0; i < NUM_PINS; i++)
        {
            // Stop looping after this buffer
            rmt_ll_enable_tx_cyclic(stepper->_hal.regs, stepper->_rmt_channel[i], false);
        }
    }

    // Wrapping increment the buffer
    stepper->_buffer_segment++;
    if (stepper->_buffer_segment >= RMT_BUFFER_COUNT)
    {
        stepper->_buffer_segment = 0;
    }
}

IRAM_ATTR void stepper_isr(void *arg)
{
    stepper_state_t *stepper = &stepper_state[0];
    DEBUG_STEPPER(stepper);
    
    uint32_t intr_st = RMT.int_st.val;
    uint8_t channel;

    for (channel = 0; channel < NUM_PINS; channel++)
    {
        int tx_done_bit = channel * 3;
        int tx_next_bit = channel + 24;

        // -- More to send on this channel
        if (intr_st & BIT(tx_next_bit))
        {
            RMT.int_clr.val |= BIT(tx_next_bit);

            // -- Refill the half of the buffer that we just finished,
            //    allowing the other half to proceed.
            stepper_fill_buffer(&stepper_state[0]);
        }
        else
        {
            // -- Transmission is complete on this channel
            if (intr_st & BIT(tx_done_bit))
            {
                RMT.int_clr.val |= BIT(tx_done_bit);
                //DEBUG_PRINT("tx complete\n")
                // We're done boys?
            }
        }
    }
    // clear interrupt
}

void stepper_start_tx(stepper_state_t *stepper)
{
    stepper->_buffer_segment = 0;
    stepper_load_next_steps(stepper);
    stepper_fill_buffer(stepper);
    for (size_t i = 0; i < NUM_PINS; i++)
    {
        // Starts transmission
        rmt_ll_reset_tx_pointer(stepper->_hal.regs, stepper->_rmt_channel[i]);
        rmt_ll_enable_tx_cyclic(stepper->_hal.regs, stepper->_rmt_channel[i], true);
        rmt_ll_start_tx(stepper->_hal.regs, stepper->_rmt_channel[i]);
    }
}

void stepper_start()
{
    stepper_start_tx(&stepper_state[0]);
}
