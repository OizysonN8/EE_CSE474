#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_periph.h"

#define LOW_FREQ 2
#define MED_FREQ 10
#define HIGH_FREQ 50
#define PWM_RESOLUTION 8
#define DUTY 128

#define LIGHT_THRESHOLD 1300
#define SEQUENCE_LENGTH_THIRD 3000000UL

#define GPIO_PIN 5
#define ANALOG_PIN 34

bool sequence_activated;
uint32_t initial;

void setup() {
  ledcAttach(GPIO_PIN, LOW_FREQ, PWM_RESOLUTION);
  sequence_activated = false;
  initial = 0;
}

void loop() {
  int analog_input = analogRead(ANALOG_PIN);

  if (analog_input >= LIGHT_THRESHOLD) {
    sequence_activated = false;
    ledcWrite(GPIO_PIN, 0);
  } else if (!sequence_activated) {
    sequence_activated = true;
    initial = micros();
    ledcChangeFrequency(GPIO_PIN, LOW_FREQ, PWM_RESOLUTION);
    ledcWrite(GPIO_PIN, DUTY);
  } else if (micros() - initial <= SEQUENCE_LENGTH_THIRD) {
    ledcChangeFrequency(GPIO_PIN, LOW_FREQ, PWM_RESOLUTION);
  } else if (micros() - initial <= SEQUENCE_LENGTH_THIRD * 2) {
    ledcChangeFrequency(GPIO_PIN, MED_FREQ, PWM_RESOLUTION);
  } else if (micros() - initial <= SEQUENCE_LENGTH_THIRD * 3) {
    ledcChangeFrequency(GPIO_PIN, HIGH_FREQ, PWM_RESOLUTION);
  } else {
    sequence_activated = false;
    ledcWrite(GPIO_PIN, 0);
  }
}