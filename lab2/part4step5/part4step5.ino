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