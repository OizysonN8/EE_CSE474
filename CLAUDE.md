# ECE/CSE 474: AI Project Context

## Board and hardware

- Board: ESP32-S3 (DevKitC-1).
- Pinout reference: [\[link\]](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html#hardware-reference)
- Peripherals I use in this course: external LEDs and 220 Ω resistors, breadboard, I2C
  LCD (PCF8574T backpack, address 0x27), tactile button, photoresistor, passive buzzer.

## Toolchain

- IDE: Arduino IDE 2.3.10
- ESP32 Arduino core: 3.3.11
- Serial monitor baud: 115200

## Datasheets and references (attach or link these for the AI)

- ESP32-S3 Technical Reference Manual (TRM): [\[link; note the pages you use\]](https://documentation.espressif.com/esp32_technical_reference_manual_en.pdf)
- PCF8574T I2C LCD backpack datasheet: [\[link\]](https://www.ti.com/lit/ds/symlink/pcf8574.pdf)
- Any peripheral datasheets for the current lab: N/A

## My coding conventions

- Follow general Doxygen naming standards (camelCase for functions, snake_case for variables, SCREAMING_CASE for macros)
- Use small functions with no magic numbers.
- Follow the Google style guide for C/C++. [\[LINK\]](https://google.github.io/styleguide/cppguide.html)

## Sample code from my past work (paste or link)

```cpp
/* Filename: part4step5.ino
 * Author: Armina Moghadasi and Nathaniel Wagner
 * Date: 2026/7/11
 * Description: This file reads from a photoresistor to direct the blinking of an
 *              LED. When the value from the photoresistor drops below a certain
 *              threshold, the LED starts a blink sequence with three stages of
 *              increasing frequency.
 */
#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_periph.h"

#define PWM_RESOLUTION 8
#define PWM_FREQ 1220
#define DUTY 255

#define LIGHT_THRESHOLD 2500
#define SEQUENCE_LENGTH_THIRD 3000000

#define GPIO_PIN 19
#define ANALOG_PIN A0

#define BAUD_RATE 115200

#define BLINK_INTERVAL_LOW 500000
#define BLINK_INTERVAL_MED 200000
#define BLINK_INTERVAL_HIGH 80000

bool     sequence_activated, led_on;
uint32_t initial_sequence, initial_blink;
uint32_t blink_interval;

void setup() {
  ledcAttach(GPIO_PIN, PWM_FREQ, PWM_RESOLUTION);
  Serial.begin(BAUD_RATE);
  sequence_activated = false;
  led_on = false;
}

void loop() {
  int analog_input = analogRead(ANALOG_PIN);
  Serial.println(analog_input);

  // Turn LED off if above threshold
  if (analog_input >= LIGHT_THRESHOLD) {
    sequence_activated = false;
    led_on = false;
    ledcWrite(GPIO_PIN, 0);
    return;  // restart loop
  }

  // Activate sequence if below threshold
  if (!sequence_activated) {
    sequence_activated = true;
    initial_sequence = micros();
    initial_blink = micros();
    led_on = false;
  }

  uint32_t elapsed = micros() - initial_sequence;

  // Determine which stage of the sequence we're in
  if (elapsed <= SEQUENCE_LENGTH_THIRD) {
    blink_interval = BLINK_INTERVAL_LOW;
  } else if (elapsed <= SEQUENCE_LENGTH_THIRD * 2) {
    blink_interval = BLINK_INTERVAL_MED;
  } else if (elapsed <= SEQUENCE_LENGTH_THIRD * 3) {
    blink_interval = BLINK_INTERVAL_HIGH;
  }

  // Toggle the LED at the corresponding frequency
  if (micros() - initial_blink >= blink_interval) {
    initial_blink = micros();
    led_on = !led_on;
    ledcWrite(GPIO_PIN, led_on ? DUTY : 0);
  }
}
```

## Things the AI keeps getting wrong (grow this list every lab)

- Assumes a classic Arduino/Uno or a generic ESP32 instead of the ESP32-S3, so pins and
  register logic come out wrong.
- Uses an outdated timer interrupt API that still takes an `edge` parameter (removed).
- Gets the PCF8574T control-bit order or the enable pulse wrong. Data goes out as two
  4-bit nibbles, each latched by pulsing enable high then low.
- Puts I2C, LCD, or `delay()` work inside an ISR instead of setting a `volatile` flag
  handled in `loop()`.
- Reaches for `analogWrite()` or `tone()` when the lab requires the LEDC functions.

## How I want the AI to help me

- Be a design partner, not a code vending machine.
- Explain before you code. When I give you a design, critique it and answer my questions
  first.
- Ask me questions when a decision is mine to make (for example, poll vs interrupt).
- Keep solutions small and readable, and tell me why each value is what it is.
- Do not dump a full finished solution unless I ask for it. I need to understand and
  defend every line at the demo.
