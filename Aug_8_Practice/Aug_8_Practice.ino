/**
 * @file Aug_8_Practice.ino
 * @author Nathaniel Wagner and Armina Moghadasi
 * @brief Blinks two external LEDs connected to an ESP32S3, one at a constant
 *        frequency and the other at a variable frequency adjusted based on
 *        potentiometer input.
 * @date 2026-08-05
 */

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" 
#include "soc/gpio_struct.h"

#define LED1_PIN 15
#define LED2_PIN 16
#define POT_PIN  17

#define STACK_ALLOC_FOR_EACH_TASK 2048

// LED Task Paramaters Struct
typedef struct {
  uint32_t pin;
  TickType_t toggle_interval_ticks;  // TickType holds the RTOS tick count 
} LedTaskParameters_t;

// Potentiometer Task Parameters Struct
typedef struct {
  uint32_t pin;
  uint64_t* pot_reading_ptr;  // Reading from potentiometer // TODO: Confirm type here
} PotTaskParameters_t;

static LedTaskParameters_t task1_params = {
  .pin = LED1_PIN,
  .toggle_interval_ticks = pdMS_TO_TICKS(500)  // 500 ms
};

static LedTaskParameters_t task2_params = {
  .pin = LED2_PIN,
  .toggle_interval_ticks = pdMS_TO_TICKS(200)  // 200 ms
};

static PotTaskParameters_t task3_params = {
  .pin = POT_PIN,
  .pot_reading_ptr = (uint64_t*) malloc (sizeof(uint64_t))
}

/**
 * @brief Defines a task that blinks an LED with a certain period.
 * 
 * @param pvParameters A pointer to the task parameters, which specify the GPIO
 * pin and period.
 */
void Task_BlinkLED(void* pvParameters) {
  LedTaskParameters_t* params = (LedTaskParameters_t*)pvParameters;
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

void Task_ReadPot(void* pvParameters) {
  PotTaskParameters_t* params = (PotTaskParameters_t*) pvParameters;
  uint32_t pin = params->pin;
  uint64_t* reading = params->pot_reading_ptr;

}

void setup() {
  xTaskCreate(
    Task_BlinkLED,
    "LED1_Task",
    STACK_ALLOC_FOR_EACH_TASK,  // Allocated stack size (words/4 bytes)
    (void *)&task1_params,
    1,  // Priority
    NULL
  );

  xTaskCreate(
    Task_BlinkLED,
    "LED2_Task",
    STACK_ALLOC_FOR_EACH_TASK,  // Allocated stack size (words/4 bytes)
    (void *)&task2_params,
    1,  // Priority
    NULL
  );

  xTaskCreate(
    Task_ReadPot,
    "Pot_Task",
    STACK_ALLOC_FOR_EACH_TASK,
    1,  // Priority
    NULL
  );
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
