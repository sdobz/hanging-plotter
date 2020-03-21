#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#include "AccelStepper.h"

extern "C" {
#include "bluetooth.h"
}

#define BLINK_GPIO (gpio_num_t)CONFIG_BLINK_GPIO

#define STEPPER1 13
#define STEPPER2 12
#define STEPPER3 14
#define STEPPER4 27

AccelStepper stepper(AccelStepper::FULL4WIRE, (gpio_num_t)STEPPER1, (gpio_num_t)STEPPER2, (gpio_num_t)STEPPER3, (gpio_num_t)STEPPER4, true);

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

void bounce(void *pvParameter)
{
    while(1) {
      if (stepper.distanceToGo() == 0) {
          stepper.moveTo(-stepper.currentPosition());
      }

      // TODO: delay until next step is due so it isn't a busy loop
      stepper.run();
      vTaskDelay(1);
    }
}

extern "C" void app_main()
{
    esp_err_t ret;
    // Initialize NVS.
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK( ret );

    bluetooth_init();
    stepper.setMaxSpeed(400);
    stepper.setAcceleration(60);
    stepper.moveTo(200);

    xTaskCreatePinnedToCore(&blink, "blink", 512,NULL,5,NULL, 1);
    xTaskCreatePinnedToCore(&bounce, "bounce", 2048,NULL,5,NULL, 1);
}