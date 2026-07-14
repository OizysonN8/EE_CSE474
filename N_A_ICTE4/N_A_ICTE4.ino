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

#define FIRST_GPIO_PIN 5
#define SECOND_GPIO_PIN 6

#define CLOCK_DIVIDER        (40000 << 13) // 80Mhz / 40000 = 2000 Hz -> 0.5 ms/tick
#define TIMER_ENABLE         (1U << 31)
#define TIMER_INCREMENT_MODE (1U << 30)

uint32_t last_ran[3] = {0, 0, 0};
uint32_t interval_in_ms[3] = {1000, 2000, 500};  // TODO: Interval for each task in ms
uint32_t interval_in_ticks[3];

void setup() {
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

  // TODO: Register setup for GPIO pins and analog read
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
    // TODO: Complete function
}

// Toggles the second LED
void taskTwo(void) {
    // TODO: Complete function
}

// Takes a reading from an analog potentiometer
// and prints it to the Serial Monitor
void taskThree(void) {
    // TODO: Complete function
}
