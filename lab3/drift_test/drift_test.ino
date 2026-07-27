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

#define SDA_PIN 17
#define SCL_PIN 18
#define LCD_ADDR 0x27

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

hw_timer_t *counterTimer = NULL;

volatile uint32_t counter = 0;
volatile bool counterUpdated = false;

volatile bool buttonPressed = false;
volatile uint32_t lastButtonInterruptMillis = 0;

void lcdPrint(const char *message) {
  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(message);
}

// Timer ISR: fires once per second based on the hardware alarm, not a
// polled comparison. Keeps work minimal - just increments the counter
// and sets a flag for loop() to pick up, since LCD/I2C calls should not
// be made directly from an ISR context.
void IRAM_ATTR onTimerInterrupt() {
  counter++;
  counterUpdated = true;
}

// Button ISR: fires on FALLING edge via attachInterrupt(). Debounces in
// software using millis(), same as before, but registration now goes
// through the single Arduino attachInterrupt() API instead of manual
// ESP-IDF gpio driver calls.
void IRAM_ATTR onButtonPress() {
  uint32_t nowMillis = millis();
  if ((nowMillis - lastButtonInterruptMillis) >= DEBOUNCE_INTERVAL_MS) {
    buttonPressed = true;
    lastButtonInterruptMillis = nowMillis;
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Button: configured entirely through the Arduino interrupt API.
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

  // Counter: configured entirely through the Arduino/ESP32 timer
  // interrupt API. timerBegin() starts the hardware timer at the given
  // tick frequency; timerAttachInterrupt() binds the ISR (edge/level
  // parameter removed in current core, timing comes from the alarm
  // config below); timerAlarm() sets the interval and enables
  // autoreload so the ISR fires every second indefinitely.
  counterTimer = timerBegin(TIMER_FREQUENCY_HZ);
  timerAttachInterrupt(counterTimer, &onTimerInterrupt);
  timerAlarm(counterTimer, TIMER_ALARM_TICKS, true, 0);
}

void loop() {
  static uint32_t lastDisplayedCounter = 0xFFFFFFFF;
  static bool wasButtonPressed = false;
  static uint32_t buttonMessageStartMillis = 0;

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
    }
  } else if (counterUpdated) {
    counterUpdated = false;

    if (counter != lastDisplayedCounter) {
      char buffer[17];
      snprintf(buffer, sizeof(buffer), "Count: %lu", (unsigned long)counter);
      lcdPrint(buffer);
      lastDisplayedCounter = counter;
    }
  }
}