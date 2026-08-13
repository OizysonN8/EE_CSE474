/**
 * @file N_A_ICTE9.ino
 * @author Nathaniel Wagner and Arm
ina Moghadasi
 * @brief Blinks an external LED connected to an ESP32-S3
 * @details This program uses FreeRTOS notifications, with one notify-er task,
 *          and a second notifyee task the toggles the LED.
 * @date 2026-08-12
 */

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ==============================================
// Pin Definitions
// ==============================================
#define LED_PIN 5
#define STACK_ALLOC_TO_EACH_TASK 4096

// ==============================================
// Task Handle(s)
// ==============================================
// TODO: Declare a global TaskHandle_t for the receiving task (BlinkTask)
// so the sender can notify it.
TaskHandle_t xBlinkHandler;
TaskHandle_t xUpdateBlinkRate;

// ==============================================
// Task A: BlinkTask (Receiver)
// ==============================================
// TODO:
//  - Configure LED pin as OUTPUT once.
//  - Block with ulTaskNotifyTake(pdTRUE, portMAX_DELAY).
//  - When a notification is received, toggle LED once.
//  - Then loop back and wait again.
void BlinkTask(void *pvParameters) {
  pinMode(LED_PIN, OUTPUT);
  bool led_on = false;
  while(true) {
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
        led_on = !led_on;
        digitalWrite(LED_PIN, led_on);
    }
  }
}


// ==============================================
// Task B: NotifierTask (Sender)
// ==============================================
// TODO:
//  - Every 1000 ms, call xTaskNotifyGive(<blink task handle>).
//  - Use vTaskDelay(pdMS_TO_TICKS(1000)) for timing (no delay()).
void NotifierTask(void *pvParameters) {
  uint32_t blink_period = 1000;

  while (true) {
    xTaskNotifyGive(xBlinkHandler);

    uint32_t received_value;
    BaseType_t xResult = xTaskNotifyWait(0x0, 0x0, &received_value, 0);
    if (xResult == pdTRUE) {
      blink_period = received_value;
    }

    vTaskDelay(pdMS_TO_TICKS(blink_period));
  }
}


// ==============================================
// Task C: ConfigTask
// ==============================================
void ConfigRate(void *pvParameters) {
  while (true) {
    uint32_t blink_period = random(500, 1500);
    xTaskNotify(xUpdateBlinkRate, blink_period, eSetValueWithOverwrite);
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}


// ==============================================
// Setup
// ==============================================
void setup() {
  Serial.begin(115200);

  // TODO: Create BlinkTask FIRST and capture its handle.
  xTaskCreate(BlinkTask, "BlinkTask", STACK_ALLOC_TO_EACH_TASK, NULL, 1, &xBlinkHandler);

  // TODO: Create NotifierTask SECOND.
  xTaskCreate(NotifierTask, "NotifierTask", STACK_ALLOC_TO_EACH_TASK, NULL, 1, &xUpdateBlinkRate);

  xTaskCreate(ConfigRate, "ConfigRate", STACK_ALLOC_TO_EACH_TASK, NULL, 1, NULL);

  // (Optional) brief print that the demo started.
  Serial.println("Task Notification demo started");
}


// ==============================================
// Loop
// ==============================================
void loop() {
  // Leave empty. FreeRTOS handles scheduling.
}