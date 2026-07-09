/* Filename: N_A_ICTE2.ino
 * Author: Armina Moghadasi and Nathaniel Wagner
 * Date: 2026/7/26
 * Description: This file controls the blinking of an external LED connected
 *              to the ESP32.
 * Details: This file features direct manipulation of the GPIO pin through
 *          register access. It has two implementations that can be commented
 *          and uncommented. The first uses the delay function, while the second
 *          produces a non-block delay with a timer.
 */
// Part 1
/*
#include "driver/gpio.h"            // For GPIO driver-level macros
#include "soc/io_mux_reg.h"         // For configuring the MUX (multiplexer)
#include "soc/gpio_reg.h"           // For direct GPIO register access
#include "soc/gpio_periph.h"        // For GPIO pin definitions

// --- Constants --- //
#define GPIO_PIN 5    // GPIO pin connected to the LED

void setup() {
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[5], PIN_FUNC_GPIO);

  // Enable GPIO_Pin as Output
  *((volatile uint32_t *) GPIO_ENABLE_REG) |= 1 << GPIO_PIN;
}

void loop() {
  // Turn the LED On
  *((volatile uint32_t *) GPIO_OUT_REG) |= 1 << GPIO_PIN;

  // Wait for 1 second
  delay(1000);

  // Turn the LED Off
  *((volatile uint32_t *) GPIO_OUT_REG) &= ~(1 << GPIO_PIN);

  // Wait again
  delay(1000);
}
*/

// Part 2
#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_periph.h"
#include "soc/timer_group_reg.h"

#define GPIO_PIN 5

#define LED_TOGGLE_INTERVAL 1000000 // for 80 MHz clock divided by 80

#define TIMER_ENABLE         (1U << 31)
#define TIMER_INCREMENT_MODE (1U << 30)

void setup() {
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[5], PIN_FUNC_GPIO);

  // Enable GPIO_Pin as Output
  *((volatile uint32_t *) GPIO_ENABLE_REG) |= 1 << GPIO_PIN;

  uint32_t timer_config = 0;

  // Apply clock divider
  timer_config |= (80 << 13);

  // Set increment mode and enable timer
  timer_config |= TIMER_INCREMENT_MODE | TIMER_ENABLE;

  // Write config to timer register
  *((volatile uint32_t *) TIMG_T0CONFIG_REG(0)) = timer_config;

  // Trigger a timer update to load settings
  *((volatile uint32_t *) TIMG_T0UPDATE_REG(0)) = 1;
}

void loop() {
  // Track last toggle time
  static uint32_t last_toggle_time = 0;

  // Refresh timer counter value
  // Done before reading to read fresh value
  *((volatile uint32_t *) TIMG_T0UPDATE_REG(0)) = 1;

  // Read current timer value
  uint32_t current_time = 0;
  current_time = *((volatile uint32_t *) TIMG_T0LO_REG(0));

  // Check if toggle interval has passed
  if ((current_time - last_toggle_time) >= LED_TOGGLE_INTERVAL) {
    // Read current GPIO output state
    uint32_t gpio_out = 0;
    gpio_out = *((volatile uint32_t *) GPIO_OUT_REG);

    // Toggle GPIO_PIN using XOR
    *((volatile uint32_t *) GPIO_OUT_REG) = gpio_out ^ (1 << GPIO_PIN);

    // Update last_toggle_time
    last_toggle_time = current_time;
  }
}
