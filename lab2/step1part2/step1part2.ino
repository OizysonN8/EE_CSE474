/* Filename: step1part2.ino
 * Author: Armina Moghadasi and Nathaniel Wagner
 * Date: 2026/7/7
 * Description: This file controls the blinking of an external LED connected
 *              to the ESP32.
 * Details: This file features direct manipulation of the GPIO pin through
 *          register access. It has two implementations that can be commented
 *          and uncommented. The first uses the delay function, while the second
 *          produces a non-block delay with a timer.
 */
#include "driver/gpio.h"            // For GPIO driver-level macros
#include "soc/io_mux_reg.h"         // For configuring the MUX (multiplexer)
#include "soc/gpio_reg.h"           // For direct GPIO register access
#include "soc/gpio_periph.h"        // For GPIO pin definitions

// --- Constants --- //
#define GPIO_PIN 5    // GPIO pin connected to the LED-

static int repetition_count;

/**************************************************
 *              Using Register Access             *
 **************************************************/
/*
void setup() {
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[5], PIN_FUNC_GPIO);

  // Enable GPIO_Pin as Output
  *((volatile uint32_t *) GPIO_ENABLE_REG) |= 1 << GPIO_PIN;
  
  repetition_count = 0;

  Serial.begin(115200);
}

void loop() {
  // Turn the LED On
  *((volatile uint32_t *) GPIO_OUT_REG) |= 1 << GPIO_PIN;

  // Wait for 1 second
  delay(10);

  // Turn the LED Off
  *((volatile uint32_t *) GPIO_OUT_REG) &= ~(1 << GPIO_PIN);

  // Wait again
  delay(10);

  ++repetition_count;

  if (repetition_count == 1000) {
    uint32_t time_to_completion = micros();
    Serial.print(time_to_completion, DEC);
    Serial.println(" microseconds");
  }
}
*/

/**************************************************
 *             Using Ardunio Functions            *
 **************************************************/
void setup() {
  pinMode(GPIO_PIN, OUTPUT);
  
  repetition_count = 0;

  Serial.begin(115200);
}

void loop() {
  // Turn the LED On
  digitalWrite(GPIO_PIN, HIGH);

  // Wait for 1 second
  delay(10);

  // Turn the LED Off
  digitalWrite(GPIO_PIN, LOW);

  // Wait again
  delay(10);

  ++repetition_count;

  if (repetition_count == 1000) {
    uint32_t time_to_completion = micros();
    Serial.print(time_to_completion, DEC);
    Serial.println(" microseconds");
  }
}
