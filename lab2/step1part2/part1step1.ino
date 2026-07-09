/* Filename: step1part2.ino
 * Author: Armina Moghadasi and Nathaniel Wagner
 * Date: 2026/7/7
 * Description: This file controls the blinking of an external LED connected
 *              to the ESP32.
 * Details: This file blinks the LED in two through direct manipulation of the
 *          GPIO pin through register access
 */

#include "driver/gpio.h"     // for configuring and controlling GPIO pins
#include "soc/io_mux_reg.h"  //defines I/O Mux registers
#include "soc/gpio_reg.h"    //GPIO hardware registrers 
#include "soc/gpio_periph.h" //peripheral information
#define GPIO_PIN 5

void setup() {
  Serial.begin(115200);

  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GPIO_PIN], PIN_FUNC_GPIO);

  volatile uint32_t* gpio_enable = (volatile uint32_t*) GPIO_ENABLE_REG;

  *gpio_enable |= (1 << GPIO_PIN);
}

void loop() {
  // turn on LED
  volatile uint32_t* gpio_out = (volatile uint32_t*)GPIO_OUT_REG;
  *gpio_out |= (1 << GPIO_PIN);
  delay(1000);

  //turn off LED
  *gpio_out &= ~(1 << GPIO_PIN);
  delay(1000);
}
