/*
 * Filename: counter_polling_library_fixed.ino
 * Author: Nathaniel Wagner
 * Description: Displays a seconds counter on an I2C LCD using only the
 *              LiquidCrystal_I2C library's built-in functions. Counter
 *              increments via polling a free-running hardware timer
 *              (TIMG0, Timer 0). A button on GPIO21 triggers a GPIO
 *              interrupt that pauses the counter and shows "Button
 *              Pressed" for 2 seconds. configureButton() now uses only
 *              the ESP-IDF gpio driver API (gpio_set_direction,
 *              gpio_set_pull_mode, gpio_set_intr_type) instead of mixing
 *              raw register writes with the driver's ISR service, which
 *              was silently overriding the manual interrupt config and
 *              preventing the button interrupt from firing.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "driver/gpio.h"
#include "soc/timer_group_reg.h"

#define SDA_PIN 17
#define SCL_PIN 18
#define LCD_ADDR 0x27

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

#define BUTTON_PIN 21
#define DEBOUNCE_INTERVAL_MS 50
#define BUTTON_MESSAGE_DURATION_MS 2000

#define TIMER_CLOCK_DIVIDER 80
#define ONE_SECOND_IN_TICKS 1000000

volatile uint32_t counter = 0;
volatile bool buttonPressed = false;
volatile uint32_t lastButtonInterruptMillis = 0;

void lcdPrint(const char *message) {
  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(message);
}

void IRAM_ATTR onButtonPress(void *arg) {
  uint32_t nowMillis = millis();
  if ((nowMillis - lastButtonInterruptMillis) >= DEBOUNCE_INTERVAL_MS) {
    buttonPressed = true;
    lastButtonInterruptMillis = nowMillis;
  }
}

void configureTimer() {
  uint32_t timerConfig = 0;
  timerConfig |= (TIMER_CLOCK_DIVIDER << 13);
  timerConfig |= TIMG_T0_INCREASE_M;
  timerConfig |= TIMG_T0_EN_M;

  *((volatile uint32_t *) TIMG_T0CONFIG_REG(0)) = timerConfig;
  *((volatile uint32_t *) TIMG_T0UPDATE_REG(0)) = 1;
}

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

  Wire.begin(SDA_PIN, SCL_PIN);

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

  *((volatile uint32_t *) TIMG_T0UPDATE_REG(0)) = 1;
  uint32_t currentTime = *((volatile uint32_t *) TIMG_T0LO_REG(0));

  if (buttonPressed) {
    if (!wasButtonPressed) {
      lcdPrint("Button Pressed");
      buttonMessageStartMillis = millis();
      wasButtonPressed = true;
    }

    if ((millis() - buttonMessageStartMillis) >= BUTTON_MESSAGE_DURATION_MS) {
      buttonPressed = false;
      wasButtonPressed = false;
      lastDisplayedCounter = 0xFFFFFFFF;
      lastIncrementTime = currentTime;
    }
  } else {
    if ((currentTime - lastIncrementTime) >= ONE_SECOND_IN_TICKS) {
      counter++;
      Serial.printf("counter = %lu\n", counter);
      lastIncrementTime = currentTime;
    }

    if (counter != lastDisplayedCounter) {
      char buffer[17];
      snprintf(buffer, sizeof(buffer), "Count: %lu", (unsigned long)counter);
      lcdPrint(buffer);
      lastDisplayedCounter = counter;
    }
  }
}