#include "feertos/FreeRTOS.h"
#include "freetos/task.h" 
#include "driver/gpio.h"
#include "soc/gpio_struct.h"

#define LED1_PIN 5
#define LED2_PIN 6

BaseType_t xTaskCreate(
  TaskFunction_t pvTaskCode, // Pointer to the function that implements the task
  const char * const pcName, // Task name (for debugging)
  configSTACK_DEPTH_TYPE usStackDepth, // Stack size (in words, NOT bytes)
  void *pvParameters, // Parameters passed to the task (NULL if not used)
  UBaseType_t uxPriority, // Task priority (higher number = higher priority)
  TaskHandle_t *pxCreatedTask // Task handle (NULL if not needed)
);

//Task Paramaters Struct
typedef struct {
  uint32_t pin;
  TickType_t toggle_interval_ticks; //TickType holds the RTOS tick count 
} LedTaskParameters_t;

static LedTaskParameters_t task1_params = {
  .pin = LED1_PIN;
  .toggle_interval_ticks = pdMS_TO_TICKS(500)
};

static LedTaskParameters_t task2_params = {
  .pin = LED2_PIN;
  .toggle_interval_ticks = pdMS_TO_TICKS(200);
}

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
    2048,
    (void *)&task1_params,
    1,
    NULL
  );
  xTaskCreate(
    Task_BlinkLED,
    "LED2_Task",
    2048,
    (void *)&task2_params,
    1,
    NULL
  );
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
