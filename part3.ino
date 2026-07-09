#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_periph.h"

const int freq = 5000;
const int resolution = 8;

const int ledPin = 40;
const int analogPin = 1;

void setup() {
  Serial.begin(115200);
  ledcAttach(ledPin, freq, resolution);

}

void loop() {
  // put your main code here, to run repeatedly:
  int analog_input = analogRead(analogPin);
  if(analog_input < 1365) {
    ledcWrite(40, (255 - (analog_input * 120 / 1365)));
  }else {
    ledcWrite(40, (85 - ((analog_input - 2730) * 120 / 1365)));
  }
}
