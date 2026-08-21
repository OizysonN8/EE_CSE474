/*
 * Isolated DHT11 test - no FreeRTOS tasks, no LCD, no sound sensor.
 * Purpose: confirm the DHT11 + wiring works correctly on its own before
 * reintroducing the rest of the system.
 *
 * Wiring: DHT11 DATA -> GPIO5, VCC -> 3.3V, GND -> GND, 10k pull-up
 * between DATA and VCC, pin 3 (NC) left unconnected.
 */

#include <DHT.h>

#define DHT_PIN  5
#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);

void setup() {
  Serial.begin(115200);
  delay(2000); // let sensor stabilize after power-up
  dht.begin();
}

void loop() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  Serial.print("Humidity: ");
  Serial.print(h);
  Serial.print(" %  Temp: ");
  Serial.print(t);
  Serial.println(" C");

  if (isnan(h) || isnan(t)) {
    Serial.println("  -> READ FAILED (NaN)");
  }

  delay(2000); // DHT11 needs >=1s between reads; 2s is safe margin
}