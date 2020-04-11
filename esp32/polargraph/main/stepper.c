#include "stepper.h"
#include <stdio.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rmt_async.h"

#include <inttypes.h>

#define NUM_PINS 4
#define NUM_STEPPERS 2
#define RMT_BUFFER_SIZE 64 // 2 items per size rmt_item32_t

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

// Array of items to transmit to a stepper control pin
typedef rmt_item32_t stepper_rmt_buffer_t[RMT_BUFFER_SIZE];

typedef struct {
    int8_t _velocity_setpoint;
    stepper_rmt_buffer_t _rmt_buffer[NUM_PINS];
} stepper_state_t;

static stepper_state_t stepper_state[NUM_STEPPERS];

static stepper_position_report_t stepper_position_reporter;

void setup_rmt(uint32_t, gpio_num_t, rmt_channel_t);
void write_step(uint32_t, uint32_t, uint8_t, uint8_t);
void write_step_double(size_t, size_t, uint8_t);

void stepper_init(gpio_num_t pins_a[NUM_PINS], stepper_position_report_t r) {
    int64_t start = esp_timer_get_time();
    stepper_position_reporter = r;

    setup_rmt(0, pins_a[0], RMT_CHANNEL_0);
    setup_rmt(0, pins_a[1], RMT_CHANNEL_1);
    setup_rmt(0, pins_a[2], RMT_CHANNEL_2);
    setup_rmt(0, pins_a[3], RMT_CHANNEL_3);
    
    size_t i;
    for (i = 0; i < RMT_BUFFER_SIZE; i++) {
        write_step_double(0, i, step_seq[i & 0xff]);
    }

    // ESP rmt has 512x32bit RAM block, thus 512 rmt_item32_ts/8 = 64 items/
    // 
    ESP_ERROR_CHECK(rmt_write_items(RMT_CHANNEL_0, stepper_state[0]._rmt_buffer[0], RMT_BUFFER_SIZE, false));
    ESP_ERROR_CHECK(rmt_write_items(RMT_CHANNEL_1, stepper_state[0]._rmt_buffer[1], RMT_BUFFER_SIZE, false));
    ESP_ERROR_CHECK(rmt_write_items(RMT_CHANNEL_2, stepper_state[0]._rmt_buffer[2], RMT_BUFFER_SIZE, false));
    ESP_ERROR_CHECK(rmt_write_items(RMT_CHANNEL_3, stepper_state[0]._rmt_buffer[3], RMT_BUFFER_SIZE, false));
    int64_t end = esp_timer_get_time();

    rmt_tx_start(RMT_CHANNEL_0, true);
    rmt_tx_start(RMT_CHANNEL_1, true);
    rmt_tx_start(RMT_CHANNEL_2, true);
    rmt_tx_start(RMT_CHANNEL_3, true);
    int64_t sent = esp_timer_get_time();

    printf("send %" PRId64 " ms transmit %" PRId64 "ms\n", end - start, sent - end);
}

void stepper_set_velocities(int8_t vel_a, int8_t vel_b) {
    stepper_state[0]._velocity_setpoint = vel_a;
}

void setup_rmt(uint32_t stepper, gpio_num_t pin, rmt_channel_t channel) {
    rmt_config_t config = RMT_DEFAULT_CONFIG_TX(pin, channel);
    config.tx_config.loop_en = false;
    config.clk_div = 255;

    ESP_ERROR_CHECK(rmt_config(&config));
    ESP_ERROR_CHECK(rmt_driver_install(config.channel, 0, 0));
}

// TODO: https://github.com/espressif/esp-idf/blob/c9f29e0b59988779bf0792e2e954c3448fd37e3b/examples/peripherals/rmt/led_strip/components/led_strip/src/led_strip_rmt_ws2812.c#L64

#define STEP_DUR 500
void write_step(uint32_t stepper, size_t offset, uint8_t mask0, uint8_t mask1) {
    rmt_item32_t *dst;
    rmt_item32_t src = {{{ STEP_DUR, 0, STEP_DUR, 0 }}};
    uint8_t i;
    for (i = 0; i < 4; i++) {
        src.level0 = mask0 & (1 << i) ? 0 : 1;
        src.level1 = mask1 & (1 << i) ? 0 : 1;
        dst = &stepper_state[stepper]._rmt_buffer[i][offset];
        dst->val = src.val;
    }
}

void write_step_double(size_t stepper, size_t offset, uint8_t mask) {
    rmt_item32_t *dst;
    rmt_item32_t src = {{{ STEP_DUR, 0, STEP_DUR, 0 }}};
    uint8_t i;
    for (i = 0; i < 4; i++) {
        src.level0 = mask & (1 << i) ? 1 : 0;
        src.level1 = src.level0;
        dst = &stepper_state[stepper]._rmt_buffer[i][offset];
        dst->val = src.val;
        printf("%i", src.level0);
    }
    printf("\n");
}