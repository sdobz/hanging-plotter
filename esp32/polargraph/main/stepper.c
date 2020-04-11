#include "stepper.h"
#include <stdio.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <driver/rmt.h>

#include <inttypes.h>

#define NUM_PINS 4
#define NUM_STEPPERS 1
#define RMT_BUFFER_SIZE 8 // number of rmt_item32_t to store in buffer, note 2 steps per item
#define RMT_BUFFER_COUNT 2 // 2 framebuffers

#define RMT_DIV 255
#define RMT_HZ 80000000

static uint8_t step_seq[] = {
    0b0001,
    0b0101,
    0b0100,
    0b0110,
    0b0010,
    0b1010,
    0b1000,
    0b1001
};

static rmt_item32_t end_marker = {{{ 0, 0, 0, 0 }}};

// Array of items to transmit to a stepper control pin
typedef rmt_item32_t stepper_rmt_buffer_t[RMT_BUFFER_SIZE];

typedef struct {
    stepper_rmt_buffer_t _rmt_buffer[NUM_PINS];
    rmt_channel_t _rmt_channel[NUM_PINS];
    
    int32_t _abs_steps;
    int16_t _steps_remaining;
    int16_t _clk_per_step;
    uint8_t _step_dir;
    uint8_t _buffer_segment;
} stepper_state_t;

static stepper_state_t stepper_state[NUM_STEPPERS];


static intr_handle_t gRMT_intr_handle = NULL;

static stepper_position_report_t stepper_position_reporter;
static stepper_get_steps_t stepper_get_steps;

void stepper_isr();

void stepper_init(gpio_num_t pins_a[NUM_PINS], rmt_channel_t channels_a[NUM_PINS], stepper_position_report_t r, stepper_get_steps_t step_getter) {
    stepper_position_reporter = r;
    stepper_get_steps = step_getter;

    size_t i;
    for (int i=0; i < NUM_PINS; i++) {
        rmt_channel_t channel = channels_a[i];
        stepper_state[0]._rmt_channel[i] = channel;
        rmt_config_t config = RMT_DEFAULT_CONFIG_TX(pins_a[i], channel);
        config.tx_config.loop_en = false;
        config.clk_div = RMT_DIV;

        ESP_ERROR_CHECK(rmt_config(&config));
        //ESP_ERROR_CHECK(rmt_driver_install(channel, 0, 0));
        for (int b=0;b<RMT_BUFFER_SIZE;b++) {
            stepper_state[0]._rmt_buffer[i][b] = end_marker;
        }
        for (int f=0;f<RMT_BUFFER_COUNT;f++) {
            // Copy the buffer into the rmt driver memory
            // For each framebuffer
            ESP_ERROR_CHECK(rmt_fill_tx_items(channel, stepper_state[0]._rmt_buffer[i], RMT_BUFFER_SIZE, RMT_BUFFER_SIZE*f));
        }
        // Copy an end marker after both buffers
        ESP_ERROR_CHECK(rmt_fill_tx_items(channel, &end_marker, 1, RMT_BUFFER_SIZE*RMT_BUFFER_COUNT + 1));
    }
    rmt_set_tx_thr_intr_en(channels_a[0], true, RMT_BUFFER_SIZE);

     if (gRMT_intr_handle == NULL)
        esp_intr_alloc(ETS_RMT_INTR_SOURCE, 0, stepper_isr, 0, &gRMT_intr_handle);
}

IRAM_ATTR void stepper_isr(void *arg) {
    uint32_t intr_st = RMT.int_st.val;
    uint8_t channel;

    for (channel = 0; channel < NUM_PINS; channel ++) {
        int tx_done_bit = channel * 3;
        int tx_next_bit = channel + 24;

        // -- More to send on this channel
        if (intr_st & BIT(tx_next_bit)) {
            RMT.int_clr.val |= BIT(tx_next_bit);
            
            // -- Refill the half of the buffer that we just finished,
            //    allowing the other half to proceed.
            stepper_fill_buffer(&stepper_state[0]);
        } else {
            // -- Transmission is complete on this channel
            if (intr_st & BIT(tx_done_bit)) {
                RMT.int_clr.val |= BIT(tx_done_bit);
                // We're done boys?
            }
        }
    }
    // clear interrupt
}

uint8_t stepper_pop_mask(stepper_state_t *stepper) {
    if (stepper->_steps_remaining == 0) {
        return 0;
    }
    uint8_t mask = step_seq[stepper->_abs_steps & 0x7];
    stepper->_abs_steps += stepper->_step_dir;
    stepper->_steps_remaining--;
    if (stepper->_steps_remaining == 0) {
        int16_t *step_info = stepper_get_steps();
        if (step_info[0] == 0) {
            // let _steps_remaining stay at 0 for all future pop_masks
            return mask;
        }
        stepper->_steps_remaining = abs(step_info[0]);
        stepper->_clk_per_step = step_info[1] * RMT_HZ / (RMT_DIV * 1000000);
        stepper->_step_dir = step_info[0] > 0 ? 1 : 0;
    }

    return mask;
}

void stepper_fill_buffer(stepper_state_t *stepper) {
    rmt_item32_t src = end_marker;
    uint8_t mask0;
    uint8_t mask1;
    uint32_t ticks0;
    uint32_t ticks1;
    for (size_t offset = 0; offset < RMT_BUFFER_SIZE; offset++) {
        mask0 = stepper_pop_mask(stepper);
        ticks0 = stepper->_clk_per_step;
        printf("tick %i\n", stepper->_abs_steps);
        mask1 = stepper_pop_mask(stepper);
        ticks1 = stepper->_clk_per_step;
        printf("tick %i\n", stepper->_abs_steps);
        for (size_t i = 0; i < NUM_PINS; i++) {
            src.level0 = mask0 & (1 << i) ? 0 : 1;
            src.duration0 = ticks0;
            src.level1 = mask1 & (1 << i) ? 0 : 1;
            src.duration1 = ticks1;
            stepper->_rmt_buffer[i][offset].val = src.val;
        }
    }

    for (size_t i = 0; i < NUM_PINS; i++) {
        // Write the buffer to the current buffer segment
        ESP_ERROR_CHECK(rmt_fill_tx_items(
            stepper->_rmt_channel[i],
            stepper->_rmt_buffer[i],
            RMT_BUFFER_SIZE,
            RMT_BUFFER_SIZE*stepper->_buffer_segment));

        // Wrapping increment the buffer
        stepper->_buffer_segment++;
        if (stepper->_buffer_segment >= RMT_BUFFER_COUNT) {
            stepper->_buffer_segment = 0;
        }
      
        if (stepper->_steps_remaining == 0) {
            // Stop looping after this buffer
            rmt_set_tx_loop_mode(stepper->_rmt_channel[i], false);
        }
    }
}

void stepper_start_tx(stepper_state_t *stepper) {
    stepper->_buffer_segment = 0;
    stepper_fill_buffer(stepper);
    for (size_t i=0; i<NUM_PINS; i++) {
        rmt_set_tx_intr_en(stepper->_rmt_channel[i], false);
        // Starts transmission
        rmt_set_tx_loop_mode(stepper->_rmt_channel[i], true);
    }
}
