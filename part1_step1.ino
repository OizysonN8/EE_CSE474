#include "driver/gpio.h" 
//contains functions for configuring and controlling GPIO pins withouth directly 
//manipulating registers. gpio_set_direction, gpio_set_level, gpio_get_level, gpio_reset_pin
#include "soc/io_mux_reg.h" //defines I/O Mux registers (what function each pin perfomrs)
#include "soc/gpio_reg.h" //GPIO hardware registrers 
#include "soc/gpio_periph.h" //peripheral information
#define GPIO_PIN 5
//remember they exisit
//GPIO_ENABLE_REG
//GPIO_OUT_REG


void setup() {
  Serial.begin(115200);
  rgbLedWrite(RGB_BUILTIN, 0, 0, 0);
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GPIO_PIN], PIN_FUNC_GPIO);
  volatile uint32_t* gpio_enable = (volatile uint32_t*)GPIO_ENABLE_REG;
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
