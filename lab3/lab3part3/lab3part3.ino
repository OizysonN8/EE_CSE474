/*
 * Filename: counter_polling_library_fixed.ino
 * Author: Nathaniel Wagner and Armina Moghadasi
 * Date: 7/26/2026
 * Description: Displays a seconds counter on an I2C LCD. A button on GPIO21
 *              triggers a GPIO interrupt that pauses the counter and shows
 *              "Button Pressed" for 2 seconds.
 */

#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#include "driver/gpio.h"
#include "soc/timer_group_reg.h"

#define SDA_PIN 17
#define SCL_PIN 18
#define LCD_ADDR 0x27

#define BUTTON_PIN 21
#define DEBOUNCE_INTERVAL_MS 50
#define BUTTON_MESSAGE_DURATION_MS 2000

// TIMG0 Timer 0 configuration: divides the 80MHz APB clock by
// TIMER_CLOCK_DIVIDER to get a 1MHz tick rate, so ONE_SECOND_IN_TICKS
// ticks equals exactly one second of elapsed time.
#define TIMER_CLOCK_DIVIDER 80
#define ONE_SECOND_IN_TICKS 1000000

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

volatile uint32_t counter = 0;
volatile bool     buttonPressed = false;
volatile uint32_t lastButtonInterruptMillis = 0;

// Clears the first 16 characters of row 0 by overwriting with spaces,
// then writes the new message starting at column 0.
void lcdPrint(const char *message) {
  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(message);
}

// Runs on every falling edge of BUTTON_PIN. Debounces in software by
// ignoring edges that occur within DEBOUNCE_INTERVAL_MS of the last
// accepted press.
void IRAM_ATTR onButtonPress(void *arg) {
  uint32_t nowMillis = millis();
  if ((nowMillis - lastButtonInterruptMillis) >= DEBOUNCE_INTERVAL_MS) {
    buttonPressed = true;
    lastButtonInterruptMillis = nowMillis;
  }
}

// Enables TIMG0 Timer 0 as a free-running up-counter.
void configureTimer() {
  uint32_t timerConfig = 0;
  timerConfig |= (TIMER_CLOCK_DIVIDER << 13);
  timerConfig |= TIMG_T0_INCREASE_M;  // sets count direction to increasing
  timerConfig |= TIMG_T0_EN_M;

  *((volatile uint32_t *) TIMG_T0CONFIG_REG(0)) = timerConfig;
  *((volatile uint32_t *) TIMG_T0UPDATE_REG(0)) = 1;
}

// Configures BUTTON_PIN entirely through the ESP-IDF gpio driver API
void configureButton() {
  gpio_num_t pin = (gpio_num_t)BUTTON_PIN;

  gpio_set_direction(pin, GPIO_MODE_INPUT);
  gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
  gpio_set_intr_type(pin, GPIO_INTR_NEGEDGE);

  gpio_install_isr_service(0);
  gpio_isr_handler_add(pin, onButtonPress, NULL);
  gpio_intr_enable(pin);
}

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);  // Overrides lcd library default pins.

  lcd.init();
  lcd.backlight();
  lcd.clear();

  configureTimer();
  configureButton();
}

void loop() {
  static uint32_t lastIncrementTime = 0;
  static uint32_t lastDisplayedCounter = 0xFFFFFFFF;
  static bool wasButtonPressed = false;
  static uint32_t buttonMessageStartMillis = 0;

  // Latch the current timer value into the read register, then read it.
  *((volatile uint32_t *) TIMG_T0UPDATE_REG(0)) = 1;
  uint32_t currentTime = *((volatile uint32_t *) TIMG_T0LO_REG(0));

  if (buttonPressed) {
    // On first detecting the press (not yet displayed), show the
    // message and start the 2-second hold timer.
    if (!wasButtonPressed) {
      lcdPrint("Button Pressed");
      buttonMessageStartMillis = millis();
      wasButtonPressed = true;
    }

    // Once the hold duration elapses, resume normal counting and force
    // the counter display to redraw on the next loop iteration.
    if ((millis() - buttonMessageStartMillis) >= BUTTON_MESSAGE_DURATION_MS) {
      buttonPressed = false;
      wasButtonPressed = false;
      lastDisplayedCounter = 0xFFFFFFFF;
      lastIncrementTime = currentTime;
    }
  } else {
    // Increment once per elapsed second, based on the hardware timer
    // rather than millis(), to avoid drift from loop() execution time.
    if ((currentTime - lastIncrementTime) >= ONE_SECOND_IN_TICKS) {
      counter++;
      Serial.printf("counter = %lu\n", counter);
      lastIncrementTime = currentTime;
    }

    // Only rewrite the LCD when the displayed value is actually stale
    if (counter != lastDisplayedCounter) {
      char buffer[17];
      snprintf(buffer, sizeof(buffer), "Count: %lu", (unsigned long)counter);
      lcdPrint(buffer);
      lastDisplayedCounter = counter;
    }
  }
}