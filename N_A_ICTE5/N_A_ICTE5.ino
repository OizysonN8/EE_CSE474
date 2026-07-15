/* Filename: N_A_ICTE5.ino
 * Author: Armina Moghadasi and Nathaniel Wagner
 * Date: 2026/7/15
 * Description: A more complex round-robin scheduler that runs three independent
 *              tasks on the ESP32-S3. The tasks are flashing and LED at a fixed
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

#define NUMBER_OF_TASKS 3

bool led1 = false;
bool led2 = false;

// Toggles the first LED
void taskZero(void);

// Toggles the second LED
void taskOne(void);

// Takes a reading from an analog potentiometer
// and prints it to the Serial Monitor
void taskTwo(void);

// Schedules which task to execute
// Paremeters: time - current time
void scheduler(uint32_t time);

// Executes a function using its pointer
// Parameters: void* - function pointer
void runTask(void*);

typedef void (*TaskFunction) (void);

typedef struct t {
  TaskFunction function_ptr;
  bool         ready;
  uint32_t     delay;
  uint32_t     last_run;
} task;

task task_list[NUMBER_OF_TASKS];

uint32_t last_time; // The last time read by the registers
uint8_t  task_index = 0;  // Current function running

void setup() {
  Serial.begin(115200);

  task task_zero = (task) {taskZero, true, 2000};  // 1 second delay
  task task_one  = (task) {taskOne, true, 4000};  // 2 second delay
  task task_two  = (task) {taskTwo, true, 1000};  // 0.5 second delay

  // Create the list of tasks
  task_list[0] = task_zero;
  task_list[1] = task_one;
  task_list[2] = task_two;

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

  if (current_time - last_time >= 2) {
    scheduler(current_time);
    current_time = last_time;
  }
}

void scheduler(uint32_t current_time) {
  if (task_list[task_index].ready &&
      (current_time - task_list[task_index].last_run >= task_list[task_index].delay)) {
    runTask(task_list[task_index].function_ptr);
    task_list[task_index].last_run = current_time;
  }
  task_index = (task_index + 1) % NUMBER_OF_TASKS;
}

void runTask(TaskFunction function_ptr) {
  function_ptr();
}

void taskZero(void) {
    volatile uint32_t* gpio_out = (volatile uint32_t*)GPIO_OUT_REG;
    led1 = !led1;
    if(led1)
      *gpio_out |= (1 << GPIO_PIN1);
    else 
      *gpio_out &= ~(1 << GPIO_PIN1);
}

// Toggles the second LED
void taskOne(void) {
    volatile uint32_t* gpio_out = (volatile uint32_t*)GPIO_OUT_REG;
    led2 = !led2;
    if(led2)
      *gpio_out |= (1 << GPIO_PIN2);
    else
      *gpio_out &= ~(1 << GPIO_PIN2);
}

// Takes a reading from an analog potentiometer
// and prints it to the Serial Monitor
void taskTwo(void) {
    int pot_value = adc1_get_raw(POT_ADC_CHANNEL);
    Serial.print("Potentiometer: ");
    Serial.println(pot_value);
}
