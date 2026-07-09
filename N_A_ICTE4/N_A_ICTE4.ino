/* Filename: N_A_ICTE4
 * Author: Armina Moghadasi and Nathaniel Wagner
 * Date: 2026/7/8
 * Description:  
 */
#include "driver/gpio.h"            // For GPIO driver-level macros
#include "soc/io_mux_reg.h"         // For configuring the MUX (multiplexer)
#include "soc/gpio_reg.h"           // For direct GPIO register access
#include "soc/gpio_periph.h"        // For GPIO pin definitions

// --- Constants --- //
#define GPIO_PIN_FIVE 5    // GPIO pin connected to the LED-
#define GPIO_PIN_SIX 6

uint32_t initial;
uint32_t pins_in_use;

void setup() {
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GPIO_PIN_FIVE], PIN_FUNC_GPIO);
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GPIO_PIN_SIX], PIN_FUNC_GPIO);

  // Enable GPIO_Pins as Output
  *((volatile uint32_t *) GPIO_ENABLE_REG) |= 1 << GPIO_PIN_FIVE;
  *((volatile uint32_t *) GPIO_ENABLE_REG) |= 1 << GPIO_PIN_SIX;

  uint_32_t pins_in_use = (1 << GPIO_PIN_FIVE) | (1 << GPIO_PIN_SIX);

  Serial.begin(115200);
  initial = millis();
}

void loop() {
  uint32_t elapsed = micros() - initial;

  if (elapsed >= 1000) {
    uint32_t gpio_out = *((volatile uint32_t *) GPIO_OUT_REG);

    *((volatile uint32_t *) GPIO_OUT_REG) = gpio_out ^ pins_in_use;

    initial = millis();
  }


}
