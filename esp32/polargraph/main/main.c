#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include <math.h>

#include "stepper.h"
#include "debug.h"

#include "bluetooth.h"

#define BLINK_GPIO (gpio_num_t) CONFIG_BLINK_GPIO

// void motor_receive_motor_set(int8_t a_vel, int8_t b_vel)

static gpio_num_t stepper_pins[NUM_STEPPERS][NUM_PINS] = {{13, 12, 14, 27}, {26, 25, 33, 32}};
static rmt_channel_t stepper_channels[NUM_STEPPERS][NUM_PINS] = {{RMT_CHANNEL_0,
                                                                  RMT_CHANNEL_1,
                                                                  RMT_CHANNEL_2,
                                                                  RMT_CHANNEL_3},
                                                                 {RMT_CHANNEL_4,
                                                                  RMT_CHANNEL_5,
                                                                  RMT_CHANNEL_6,
                                                                  RMT_CHANNEL_7}};

void blink(void *pvParameter)
{
    gpio_pad_select_gpio(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    while (1)
    {
        /* Blink off (output low) */
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(1000 / portTICK_RATE_MS);
        /* Blink on (output high) */
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

static bool auto_step = true;
static uint8_t auto_step_phase = 0;
static uint8_t auto_step_count = 0;

static stepper_task_t auto_step_seq[] = {
    {{50, 0}, 10000},
    {{30, 30}, 10000},
    {{0, 50}, 10000},
    {{-30, 30}, 10000},

    {{-50, 0}, 10000},
    {{-30, -30}, 10000},
    {{0, -50}, 10000},
    {{30, -30}, 10000},
};

static int8_t last_vel_a = 0;
static int8_t last_vel_b = 0;
static stepper_task_t step_task = {{5, 6}, 10000};
void stepper_set_velocities(int8_t vel_a, int8_t vel_b)
{
    auto_step = false;
    step_task.steps[0] = vel_a;
    step_task.steps[1] = vel_b;
    if ((last_vel_a == 0 && vel_a != 0) || (last_vel_b == 0 && vel_b != 0))
    {
        stepper_start();
    }
    last_vel_a = vel_a;
    last_vel_b = vel_b;
}

stepper_task_t *(*stepper_get_task())
{
    if (auto_step)
    {
        auto_step_count++;
        if (auto_step_count > 10)
        {
            auto_step_count = 0;
            auto_step_phase = (auto_step_phase + 1) & 0x7;
        }
        DEBUG_PRINT("PHASE: %d\n", auto_step_phase);
        return &auto_step_seq[auto_step_phase];
    }
    return (int32_t *)&step_task;
}

void app_main()
{
    esp_err_t ret;
    // Initialize NVS.
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);

    bluetooth_init(stepper_set_velocities);
    stepper_init(stepper_pins, stepper_channels, bluetooth_motor_position, stepper_get_task);
    stepper_start();
    xTaskCreatePinnedToCore(&blink, "blink", 512, NULL, 5, NULL, 1);
}
