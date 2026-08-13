/**
 * @file N_A_icte7.ino
 * @author Armina Moghadasi and Nate Wagner
 * @brief This file blinks two external LEDs connected to an ESP32-S3, one every
 * 500 ms and the other every 200 ms.
 *
 * @details The freertos library is used to create two tasks that are scheduled
 * by FreeRTOS's round robin in a non-blocking manner.
 */

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" 
#include "soc/gpio_struct.h"

#define LED1_PIN 15
#define LED2_PIN 16

//Task Paramaters Struct
typedef struct {
  uint32_t pin;
  TickType_t toggle_interval_ticks;  //TickType holds the RTOS tick count 
} LedTaskParameters_t;

static LedTaskParameters_t task1_params = {
  .pin = LED1_PIN,
  .toggle_interval_ticks = pdMS_TO_TICKS(500)  // 500 ms
};

static LedTaskParameters_t task2_params = {
  .pin = LED2_PIN,
  .toggle_interval_ticks = pdMS_TO_TICKS(200)  // 200 ms
};

/**
 * @brief Defines a task that blinks an LED with a certain period.
 * 
 * @param pvParameters A pointer to the task parameters, which specify the GPIO
 * pin and period.
 */
void Task_BlinkLED(void *pvParameters) {
  LedTaskParameters_t *params = (LedTaskParameters_t*)pvParameters;
  uint32_t pin = params->pin;
  TickType_t interval = params->toggle_interval_ticks;

  GPIO.enable_w1ts = (1UL << pin); //enable output at pin

  TickType_t last_toggle_time = xTaskGetTickCount();

  while(1) {
    TickType_t current_time = xTaskGetTickCount();

    if((current_time - last_toggle_time) >= interval) {
      last_toggle_time = current_time;

      if(GPIO.out & (1UL << pin)) {
        GPIO.out_w1tc = (1UL << pin); //set pin LOW
      } else {
        GPIO.out_w1ts = (1UL << pin); //set pin HIGH
      }
    }
  }
}

void setup() {
  xTaskCreate(
    Task_BlinkLED,
    "LED1_Task",
    2048,  // Allocated stack size (words/4 bytes)
    (void *)&task1_params,
    1,  // Priority
    NULL
  );

  xTaskCreate(
    Task_BlinkLED,
    "LED2_Task",
    2048,  // Allocated stack size (words/4 bytes)
    (void *)&task2_params,
    1,  // Priority
    NULL
  );
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
