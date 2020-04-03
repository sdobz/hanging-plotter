#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#include "stepper.h"
#include "bluetooth.h"

#define BLINK_GPIO (gpio_num_t)CONFIG_BLINK_GPIO

// void motor_receive_motor_set(int8_t a_vel, int8_t b_vel)


static gpio_num_t motor_a_pins[4] = {13, 12, 14, 27};

void blink(void *pvParameter)
{
    
    gpio_pad_select_gpio(BLINK_GPIO);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
    while(1) {
        /* Blink off (output low) */
        gpio_set_level(BLINK_GPIO, 0);
        vTaskDelay(1000 / portTICK_RATE_MS);
        /* Blink on (output high) */
        gpio_set_level(BLINK_GPIO, 1);
        vTaskDelay(1000 / portTICK_RATE_MS);
    }
}

void app_main()
{
    esp_err_t ret;
    // Initialize NVS.
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK( ret );

    bluetooth_init(stepper_set_velocities);
    stepper_init(motor_a_pins, bluetooth_motor_position);

    xTaskCreatePinnedToCore(&blink, "blink", 512,NULL,5,NULL, 1);
}
