#include <Wire.h>

#define SDA_PIN 17
#define SCL_PIN 18

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.println("Scanning I2C bus...");

  int devicesFound = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Device found at address 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
      devicesFound++;
    }
  }

  if (devicesFound == 0) {
    Serial.println("No I2C devices found.");
  } else {
    Serial.print(devicesFound);
    Serial.println(" device(s) found.");
  }
}

void loop() {
  // Intentionally empty; scan runs once in setup.
}