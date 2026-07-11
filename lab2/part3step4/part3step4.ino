/* Filename: part3.ino
 * Author: Armina Moghadasi and Nathaniel Wagner
 * Date: 7/11/2026
 * Description: This file controls the brightness of an external LED through photoresister readings. 
 * Details: Photoresister values are read continously and the analog input is configured into a PWM
 with a frequncy of 5000 hz, a resolution of 8 bits, and a duty cycle based on theh analog-input. 
 */
const int freq = 5000;
const int resolution = 8;

const int ledPin = 40;
const int analogPin = 1;

void setup() {
  Serial.begin(115200);
  ledcAttach(ledPin, freq, resolution);

}

void loop() {
  int analog_input = analogRead(analogPin);
  //max brigthness of LED is 255 (2^8 resolution)
  //max reading from photoresister is 4095 (2^12 -> 12 bit ADC)
  ledcWrite(40, (255 - (analog_input * 255 / 4095)));
  //dark analog 0: 255 - 0 *255/4095 = 255 -> max bright
  //bright analog 4095: 255 - (4095 * 255)/4095 = 0  -> led off
}
