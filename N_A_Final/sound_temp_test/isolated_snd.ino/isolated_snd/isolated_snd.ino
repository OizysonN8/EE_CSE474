/*
 * Isolated sound sensor test - no FreeRTOS tasks, no LCD, no DHT11.
 * Purpose: confirm the sound sensor's AO output responds to actual sound
 * before reintroducing it into the full system.
 *
 * Wiring: Sound sensor AO -> GPIO2 (ADC1_CH1), VCC -> 3.3V, GND -> GND.
 * Potentiometer set to roughly mid-travel for max headroom.
 */

#define SOUND_PIN 2
#define ADC_MAX   4095

int minVal = ADC_MAX;
int maxVal = 0;

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  delay(500);
}

void loop() {
  int raw = analogRead(SOUND_PIN);

  if (raw < minVal) minVal = raw;
  if (raw > maxVal) maxVal = raw;

  Serial.printf("raw=%4d  min=%4d  max=%4d  swing=%4d\n",
                raw, minVal, maxVal, maxVal - minVal);

  delay(50); // fast enough to catch real fluctuation, slow enough to read
}