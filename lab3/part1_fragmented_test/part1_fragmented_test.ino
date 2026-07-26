#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define SDA_PIN 17
#define SCL_PIN 18
#define LCD_ADDR 0x27

#define RS_BIT 0x08
#define RW_BIT 0x02
#define EN_BIT 0x04
#define BACKLIGHT_BIT 0x01

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

void pulseEnable(uint8_t data) {
  Wire.beginTransmission(LCD_ADDR);
  Wire.write(data & ~EN_BIT);
  Wire.endTransmission();
  delayMicroseconds(5);

  Wire.beginTransmission(LCD_ADDR);
  Wire.write(data | EN_BIT);
  Wire.endTransmission();
  delayMicroseconds(5);

  Wire.beginTransmission(LCD_ADDR);
  Wire.write(data & ~EN_BIT);
  Wire.endTransmission();
  delayMicroseconds(100);
}

void sendNibble(uint8_t nibble, bool isData) {
  uint8_t packet = (nibble << 4) | BACKLIGHT_BIT;
  if (isData) {
    packet |= RS_BIT;
  }
  pulseEnable(packet);
}

void sendByte(uint8_t value, bool isData) {
  sendNibble((value >> 4) & 0x0F, isData);
  sendNibble(value & 0x0F, isData);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  delay(2);
}

void loop() {
  static bool messageSent = false;
  if (!messageSent) {
    const char message[] = "Hello World";
    for (size_t i = 0; i < strlen(message); i++) {
      sendByte((uint8_t)message[i], true);
    }
    messageSent = true;
  }
}