/*
 * Filename: lab3par3.ino
 * Author: Armina Moghadasi and Nathaniel Wagner
 * Description: Displays a seconds counter on an I2C LCD, incremented by a
 *              hardware timer interrupt. A button triggers a "Button Pressed"
 *              message on the LCD for 2 seconds before the counter resumes.
 */

#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#define SDA_PIN 17
#define SCL_PIN 18
#define LCD_ADDR 0x27

// LiquidCrystal_I2C handles all low-level PCF8574T/I2C command timing
// internally, so we never hand-roll nibble/byte writes ourselves.
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

#define BUTTON_PIN 21
#define DEBOUNCE_INTERVAL_MS 50
#define BUTTON_MESSAGE_DURATION_MS 2000

// Hardware timer fires once per second. 1,000,000 ticks at a 1MHz tick rate
// equals exactly 1 second
#define TIMER_FREQUENCY_HZ 1000000
#define TIMER_ALARM_TICKS 1000000

hw_timer_t *counterTimer = NULL;

volatile uint32_t counter = 0;
volatile bool counterUpdated = false;
volatile bool buttonPressed = false;
volatile uint32_t lastButtonInterruptMillis = 0;

// Clears the first 16 characters of row 0 by overwriting with spaces,
// then writes the new message starting at column 0.
void lcdPrint(const char *message) {
  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(message);
}

// Timer ISR: fires once per second based on the hardware alarm.
void IRAM_ATTR onTimerInterrupt() {
  counter++;
  counterUpdated = true;
}

// Button ISR: fires on FALLING edge via attachInterrupt(). Debounces in
// software
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
  // which would otherwise default to GPIO21 and collide with
  // BUTTON_PIN.
  Wire.begin(SDA_PIN, SCL_PIN);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Button: configured entirely through the Arduino interrupt API.
  // INPUT_PULLUP means the pin reads HIGH when idle and LOW when
  // pressed (assuming a normally-open button wired to ground), which
  // is why we trigger on FALLING.
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

  // Counter: configured entirely through the Arduino/ESP32 timer
  // interrupt API.
  //   timerBegin()            - starts the hardware timer at the given
  //                              tick frequency (1MHz here).
  //   timerAttachInterrupt()  - binds onTimerInterrupt() as the ISR.
  //                              The old edge/level parameter is gone
  //                              in the current core; timing is instead
  //                              driven entirely by the alarm config
  //                              set below.
  //   timerAlarm()            - sets the alarm value (interval, in
  //                              ticks), enables autoreload (true) so
  //                              it fires repeatedly forever, and sets
  //                              the reload count (0 = default/once
  //                              per alarm period).
  counterTimer = timerBegin(TIMER_FREQUENCY_HZ);
  timerAttachInterrupt(counterTimer, &onTimerInterrupt);
  timerAlarm(counterTimer, TIMER_ALARM_TICKS, true, 0);
}

void loop() {
  // lastDisplayedCounter starts at an impossible sentinel value so the
  // very first counter value (0) is guaranteed to trigger an initial
  // LCD write.
  static uint32_t lastDisplayedCounter = 0xFFFFFFFF;
  static bool wasButtonPressed = false;
  static uint32_t buttonMessageStartMillis = 0;

  if (buttonPressed) {
    // On first detecting the press (not yet displayed), show the
    // message and start the 2-second hold timer. Note: the counter
    // ISR keeps incrementing counter in the background during this
    // window - only the LCD redraw is suppressed, so the displayed
    // count picks back up from the current value once the hold ends.
    if (!wasButtonPressed) {
      lcdPrint("Button Pressed");
      buttonMessageStartMillis = millis();
      wasButtonPressed = true;
    }

    // Once the hold duration elapses, resume normal counter display
    // and force a redraw on the next loop iteration via the sentinel.
    if ((millis() - buttonMessageStartMillis) >= BUTTON_MESSAGE_DURATION_MS) {
      buttonPressed = false;
      wasButtonPressed = false;
      lastDisplayedCounter = 0xFFFFFFFF;
    }
  } else if (counterUpdated) {
    // Clear the flag first so we don't miss/duplicate an update if the
    // ISR fires again while we're in the middle of handling this one.
    counterUpdated = false;

    // Only rewrite the LCD when the displayed value is actually stale,
    // to avoid unnecessary I2C traffic on every loop iteration.
    if (counter != lastDisplayedCounter) {
      char buffer[17];
      snprintf(buffer, sizeof(buffer), "Count: %lu", (unsigned long)counter);
      lcdPrint(buffer);
      lastDisplayedCounter = counter;
    }
  }
}