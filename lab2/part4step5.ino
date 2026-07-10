#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_periph.h"

#define BAUD_RATE 115200;

#define FREQUENCY 5000;
#define PWM_RESOLUTION 8;
#define MAX_DUTY 255;

#define LIGHT_THRSHOLD 1300;
#define SEQUENCE_LENGTH_THIRD = 3000000;

#define GPIO_PIN 40;
#define ANALOG_PIN 1;

bool sequence_activated;
uint32_t initial;

void setup() {
  Serial.begin(BAUD_RATE);
  ledcAttach(GPIO_PIN, FREQUENCY, PWM_RESOLUTION);

  PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[GPIO_PIN], PIN_FUNC_GPIO);

  // Enable GPIO_Pin as Output
  *((volatile uint32_t *) GPIO_ENABLE_REG) |= 1 << GPIO_PIN;

  sequence_activated = false;

  initial = 0;
}

void loop() {
  // put your main code here, to run repeatedly:
  int analog_input = analogRead(analogPin);

  if ((analog_input < LIGHT_THRESHOLD) && !sequence_activated) {
    sequence_activated = true;
    initial = micros();
  } else if (sequence_activated && (micros() - initial <= SEQUENCE_LENGTH_THIRD) {
    ledcWrite(GPIO_PIN, (MAX_DUTY / 3));
  } else if (sequence_activated && (micros() - initial <= SEQUENCE_LENGTH_THIRD * 2) {
    ledcWrite(GPIO_PIN, (MAX_DUTY / 3 * 2));
  } else if (sequence_activated && (micros() - initial <= SEQUENCE_LENGTH_THIRD * 3) {
    ledcWrite(GPIO_PIN, (MAX_DUTY));
  } else {
    sequence_activated = false;
    ledcWrite(GPIO_PIN, 0);
  }
}
