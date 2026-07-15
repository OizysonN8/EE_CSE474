/* Filename: N_A_ICTE4.ino
 * Author: Armina Moghadasi and Nathaniel Wagner
 * Date: 2026/7/13
 * Description: A simple round-robin scheduler that runs three independent tasks
 *              on the ESP32-S3. The tasks are flashing and LED at a fixed
 *              interval, flashing a second LED at a different interval, and
 *              reading an analog potentiometer value before printing it to the
 *              Serial Monitor.
 */
#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_periph.h"
#include "soc/timer_group_reg.h"
#include "driver/adc.h" 

#define GPIO_PIN1 5
#define GPIO_PIN2 6

#define CLOCK_DIVIDER        (40000 << 13) // 80Mhz / 40000 = 2000 Hz -> 0.5 ms/tick
#define TIMER_ENABLE         (1U << 31)
#define TIMER_INCREMENT_MODE (1U << 30)
#define POT_ADC_CHANNEL ADC1_CHANNEL_0  // ADC1_CHANNEL_0 maps to GPIO1 on ESP32-S3

uint32_t last_ran[3] = {0, 0, 0};
uint32_t interval_in_ms[3] = {1000, 2000, 500};  // Interval for each task in ms
uint32_t interval_in_ticks[3];

bool led1 = false;
bool led2 = false;

void setup() {
  Serial.begin(115200);

  // Convert ms to ticks
  for (int i = 0; i < 3; i++) {
    interval_in_ticks[i] = interval_in_ms[i] * 2;
  }

  uint32_t timer_config = 0;
  timer_config |= CLOCK_DIVIDER;  // Apply clock divider

  // Set increment mode and enable timer
  timer_config |= TIMER_INCREMENT_MODE | TIMER_ENABLE;  

  // Write config to timer register
  *((volatile uint32_t *) TIMG_T0CONFIG_REG(0)) = timer_config;

  // Trigger a timer update to load settings
  *((volatile uint32_t *) TIMG_T0UPDATE_REG(0)) = 1;

  //specify pin function as GPIO input
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GPIO_PIN1], PIN_FUNC_GPIO);
  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GPIO_PIN2], PIN_FUNC_GPIO);

  //initalize gpio enable for ledpin1
  volatile uint32_t* gpio_enable1 = (volatile uint32_t*) GPIO_ENABLE_REG;
  *gpio_enable1 |= (1 << GPIO_PIN1);

  //intialize gpio enable for ledpin2
  volatile uint32_t* gpio_enable2 = (volatile uint32_t*) GPIO_ENABLE_REG;
  *gpio_enable2 |= (1 << GPIO_PIN2);

  //set up analog reading
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(POT_ADC_CHANNEL, ADC_ATTEN_DB_11);
}

void loop() {
  // Refresh timer counter value
  // Done before reading to read fresh value
  *((volatile uint32_t *) TIMG_T0UPDATE_REG(0)) = 1;

  // Read current timer value
  uint32_t current_time = *((volatile uint32_t *) TIMG_T0LO_REG(0));

  for (int i = 0; i < 3; i++) {
    if (current_time - last_ran[i] >= interval_in_ticks[i]) {
      last_ran[i] = current_time;
      runTask(i);
    }
  }
}

/* Switches between tasks based on an index
 *
 * Paremeters:
 *  - task_index: the index of the task to execute
 */
void runTask(int task_index) {
  switch (task_index) {
    case 0: taskOne(); break;
    case 1: taskTwo(); break;
    case 2: taskThree(); break;
  }
}

// Toggles the first LED
void taskOne(void) {
    volatile uint32_t* gpio_out = (volatile uint32_t*)GPIO_OUT_REG;
    led1 = !led1;
    if(led1)
      *gpio_out |= (1 << GPIO_PIN1);
    else 
      *gpio_out &= ~(1 << GPIO_PIN1);
}

// Toggles the second LED
void taskTwo(void) {
    volatile uint32_t* gpio_out = (volatile uint32_t*)GPIO_OUT_REG;
    led2 = !led2;
    if(led2)
      *gpio_out |= (1 << GPIO_PIN2);
    else
      *gpio_out &= ~(1 << GPIO_PIN2);
}

// Takes a reading from an analog potentiometer
// and prints it to the Serial Monitor
void taskThree(void) {
    int pot_value = adc1_get_raw(POT_ADC_CHANNEL);
    Serial.print("Potentiometer: ");
    Serial.println(pot_value);
}
