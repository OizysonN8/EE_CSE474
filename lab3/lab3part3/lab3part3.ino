/*
 * Filename: counter_interrupt_api.ino
 * Author: Nathaniel Wagner
 * Description: Displays a seconds counter on an I2C LCD, incremented by
 *              a true hardware timer interrupt (timerAttachInterrupt),
 *              rather than polling a timer register in loop(). A button
 *              on BUTTON_PIN uses attachInterrupt() with FALLING mode to
 *              trigger a "Button Pressed" message on the LCD for 2
 *              seconds before the counter resumes. Uses only the
 *              Arduino/ESP32 core interrupt APIs, no raw ESP-IDF gpio
 *              driver calls and no raw TIMG register access.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// I2C pins used to talk to the LCD's PCF8574T backpack. Explicitly
// passed to Wire.begin() so the bus does NOT fall back to the ESP32's
// default SDA pin (GPIO21), which would collide with BUTTON_PIN below.
#define SDA_PIN 17
#define SCL_PIN 18
#define LCD_ADDR 0x27

// LiquidCrystal_I2C handles all low-level PCF8574T/I2C command timing
// internally, so we never hand-roll nibble/byte writes ourselves.
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

#define BUTTON_PIN 21
#define DEBOUNCE_INTERVAL_MS 50
#define BUTTON_MESSAGE_DURATION_MS 2000

// Hardware timer fires once per second. timerBegin() takes the desired
// tick frequency in Hz (1,000,000 = 1 tick per microsecond), and
// timerAlarm() sets the alarm value in ticks, so 1,000,000 ticks at a
// 1MHz tick rate equals exactly 1 second between interrupts.
#define TIMER_FREQUENCY_HZ 1000000
#define TIMER_ALARM_TICKS 1000000

// Handle for the hardware timer instance, created in setup().
hw_timer_t *counterTimer = NULL;

// counter is incremented from within the timer ISR, so it must be
// volatile to prevent the compiler from caching stale copies in loop().
volatile uint32_t counter = 0;

// Set true by the timer ISR whenever counter changes, so loop() knows
// a redraw may be needed without having to compare timestamps itself.
volatile bool counterUpdated = false;

// Set true by the button ISR on a valid (debounced) press.
volatile bool buttonPressed = false;

// Tracks the last accepted button-press time for software debouncing.
volatile uint32_t lastButtonInterruptMillis = 0;

// Clears the first 16 characters of row 0 by overwriting with spaces,
// then writes the new message starting at column 0. Using only the
// library's setCursor()/print() API avoids any raw bus manipulation.
void lcdPrint(const char *message) {
  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(message);
}

// Timer ISR: fires once per second based on the hardware alarm itself,
// not a polled comparison in loop(). Kept intentionally minimal - just
// increments the counter and raises a flag for loop() to act on, since
// I2C/LCD calls should never be made directly from ISR context.
void IRAM_ATTR onTimerInterrupt() {
  counter++;
  counterUpdated = true;
}

// Button ISR: fires on FALLING edge via attachInterrupt(). Debounces in
// software using millis() so electrical noise or switch bounce doesn't
// register as multiple presses. Registration goes through the single
// Arduino attachInterrupt() API rather than manual ESP-IDF gpio driver
// calls, avoiding any conflict between competing configuration paths.
void IRAM_ATTR onButtonPress() {
  uint32_t nowMillis = millis();
  if ((nowMillis - lastButtonInterruptMillis) >= DEBOUNCE_INTERVAL_MS) {
    buttonPressed = true;
    lastButtonInterruptMillis = nowMillis;
  }
}

void setup() {
  Serial.begin(115200);

  // Must be called explicitly with our custom pins BEFORE lcd.init(),
  // since lcd.init() calls Wire.begin() internally with no arguments,
  // which would 