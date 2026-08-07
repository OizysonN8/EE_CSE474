/* Filename: lab3part1.ino
 * Author: Armina Moghadasi and Nathaniel Wagner
 * Date: 2026/7/25
 * Description: Displays Serial input on an LCD communicating via I2C
 */

#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#define SDA_PIN 17
#define SCL_PIN 18
#define LCD_ADDR 0x27  // I2C address of PCF8575T backpack

/*
helpful map: bytes = 8 bits
Bit Position:   [ 7 | 6 | 5 | 4 ]  [   3   |  2   |  1   |  0   ]
Function:       [  Data Nibble  ]  [ Light | Enable| R/W |  RS  ]

Bit 0: Register Select 
Bit 1: Read/Write
Bit 2: Enable
Bit 3: BackLight
*/

#define RS_BIT 0x01 //0000 0001
#define RW_BIT 0x02 //0000 0010
#define EN_BIT 0x04 //0000 0100
#define BACKLIGHT_BIT 0x08 //0000 1000

// LCD Object for setup
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

void writeToI2C(uint8_t data) {
  Wire.beginTransmission(LCD_ADDR);
  Wire.write(data);
  Wire.endTransmission();
}

void pulseEnable(uint8_t data) {
  writeToI2C(data | EN_BIT); //sets EN_BIT to 1 signalling incoming bit
  delayMicroseconds(1);
  writeToI2C(data & ~EN_BIT); //sets EN_BIt to 0 creating a fall edge
  delayMicroseconds(50);
}

void sendNibble(uint8_t nibble, bool isData) {
  uint8_t packet = (nibble << 4) | BACKLIGHT_BIT;
  if (isData) {
    packet |= RS_BIT; //sets RS_BIT to 1 if data
  }
  pulseEnable(packet);
}

// Sends a byte of data, value, to the LCD.
// isData must be true.
void sendByte(uint8_t value, bool isData) {
  sendNibble((value >> 4) & 0x0F, isData);
  sendNibble(value & 0x0F, isData);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  delay(2);
}

void loop() {
  if(Serial.available() > 0) {
    sendByte(0x01, false); //clears display
    delay(2);
    sendByte(0x80, false); //return home and set cursor to line 1

    while(Serial.available() > 0) {
      char c = Serial.read();

      if(c != '\n' && c != '\r') {
        sendByte((uint8_t)c, true);
      }
      delay(5);
    }
  }
}