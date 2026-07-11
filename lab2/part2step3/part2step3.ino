/* Filename: step2step3.ino
 * Author: Armina Moghadasi and Nathaniel Wagner
 * Date: 2026/7/9
 * Description: This module is used to compare the speed of the arduino macro
 *              functions in comparison to direct register access
 */
#include "driver/gpio.h"            // For GPIO driver-level macros
#include "soc/io_mux_reg.h"         // For configuring the MUX (multiplexer)
#include "soc/gpio_reg.h"           // For direct GPIO register access
#include "soc/gpio_periph.h"        // For GPIO pin definitions

// --- Constants --- //
#define GPIO_PIN_FIVE 5    // GPIO pin connected to the LED-
#define GPIO_PIN_SIX 6

#define ITERATIONS 1000

/**************************************************
 *              Using Register Access             *
 **************************************************/
void setup() {
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GPIO_PIN_FIVE], PIN_FUNC_GPIO);

  // Enable GPIO_Pin as Output
  *((volatile uint32_t *) GPIO_ENABLE_REG) |= 1 << GPIO_PIN_FIVE;

  pinMode(GPIO_PIN_SIX, OUTPUT);

  Serial.begin(115200);
}

void loop() {
  uint32_t initial = micros();

  // Blink LED ITERATIONS times using direct register manipulation
  for (int i = 0; i < ITERATIONS; ++i) {
    *((volatile uint32_t *) GPIO_OUT_REG) |= 1 << GPIO_PIN_FIVE;
    *((volatile uint32_t *) GPIO_OUT_REG) &= ~(1 << GPIO_PIN_FIVE);
  }

  uint32_t elapsed = micros() - initial;
  Serial.print("Register Access: ");
  Serial.print(elapsed, DEC);
  Serial.println(" microseconds");

  delay(1000);

  initial = micros();
  
  // Blink LED ITERATIONS times using the arduino macro functions
  for (int j = 0; j < ITERATIONS; ++j) {
    digitalWrite(GPIO_PIN_SIX, HIGH);
    digitalWrite(GPIO_PIN_SIX, LOW);
  }

  elapsed = micros() - initial;
  Serial.print("Arduino Language Functions: ")
  Serial.print(elapsed, DEC);
  Serial.println(" microseconds");
}
