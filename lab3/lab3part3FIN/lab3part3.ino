/*
 * Filename: lab3par3.ino
 * Author: Armina Moghadasi and Nathaniel Wagner
 * Description: Displays a seconds counter on an I2C LCD, incremented by a
 *              hardware timer interrupt. A button triggers a "Button Pressed"
 *              message on the LCD for 2 seconds before the counter resumes.
 */

#include <LiquidCrystal_I2C.h>
#include <Wire.h>

//BLE Headers
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#define SDA_PIN 17
#define SCL_PIN 18
#define LCD_ADDR 0x27


// LiquidCrystal_I2C handles all low-level PCF8574T/I2C command timing
// internally, so we never hand-roll nibble/byte writes ourselves.
LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

#define BUTTON_PIN 21
#define DEBOUNCE_INTERVAL_MS 50
#define BUTTON_MESSAGE_DURATION_MS 2000
#define DISPLAY_HOLD_MS 2000

// Hardware timer fires once per second. 1,000,000 ticks at a 1MHz tick rate
// equals exactly 1 second
#define TIMER_FREQUENCY_HZ 1000000
#define TIMER_ALARM_TICKS 1000000

// Service and Characteristic UUIDs
//UUID is Univrsally Unique Identifier. 128-bit number formatted as 32 hex characters.
//Because there are sso many possible combinations, the chances of two generated UUIDs 
//colliding are practically zero.
#define SERVICE_UUID        "19B10011-E8F2-537E-4F6C-D104768A1214"  //12
#define CHARACTERISTIC_UUID "19B10012-E8F2-537E-4F6C-D104768A1214" //13

hw_timer_t *counterTimer = NULL;

volatile uint32_t counter = 0;
volatile bool counterUpdated = false;
volatile bool buttonPressed = false;
volatile uint32_t lastButtonInterruptMillis = 0;

//Ble message
volatile bool bleMessageReceived = false;

// Clears the first 16 characters of row 0 by overwriting with spaces,
// then writes the new message starting at column 0.
void lcdPrint(const char *message) {
  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  lcd.print(message);
}

//BLeE callback that trigeers when LightBlue writes data to the characteristic
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        bleMessageReceived = true;
    }
};

// Timer ISR: fires once per second based on the hardware alarm.
void IRAM_ATTR onTimerInterrupt() {
  counter++;
  counterUpdated = true;
}

// Button ISR: fires on FALLING edge via attachInterrupt(). Debounces in
// software
void IRAM_ATTR onButtonPress() {
  uint32_t nowMillis = millis();
  if ((nowMillis - lastButtonInterruptMillis) >= DEBOUNCE_INTERVAL_MS) {
    buttonPressed = true;
    lastButtonInterruptMillis = nowMillis;
  }
}

void setup() {
  Serial.begin(115200);

  // Overwrites default GPIO pin assignment in lcd.init()
  Wire.begin(SDA_PIN, SCL_PIN);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onButtonPress, FALLING);

  counterTimer = timerBegin(TIMER_FREQUENCY_HZ);
  timerAttachInterrupt(counterTimer, &onTimerInterrupt);
  timerAlarm(counterTimer, TIMER_ALARM_TICKS, true, 0);

  //BLE Setup
  BLEDevice::init("ESP32_LCD_Counter");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID,
                                          BLECharacteristic::PROPERTY_READ |
                                          BLECharacteristic::PROPERTY_WRITE
                                        );
  pCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();

  //Start BLE advertising
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();
}

void loop() {
  static uint32_t lastDisplayedCounter = 0xFFFFFFFF;
  static bool wasButtonPressed = false;
  static uint32_t buttonMessageStartMillis = 0;

  //track BleMessages 
  static bool wasBleMsgDisplayed = false;
  static uint32_t bleMessageStartMillis = 0;

  if (buttonPressed) {
    // On first detecting the press (not yet displayed), show the
    // message and start the 2-second hold timer.
    if (!wasButtonPressed) {
      lcdPrint("Button Pressed");
      buttonMessageStartMillis = millis();
      wasButtonPressed = true;
    }

    // Once the hold duration elapses, resume normal counter display
    // and force a redraw on the next loop iteration via the sentinel.
    if ((millis() - buttonMessageStartMillis) >= BUTTON_MESSAGE_DURATION_MS) {
      buttonPressed = false;
      wasButtonPressed = false;
      lastDisplayedCounter = 0xFFFFFFFF;
    }
  } else if(bleMessageReceived) {
    if (!wasBleMsgDisplayed) {
      lcdPrint("New Message!");
      bleMessageStartMillis = millis();
      wasBleMsgDisplayed = true;
    }

    if ((millis() - bleMessageStartMillis) >= DISPLAY_HOLD_MS) {
      bleMessageReceived = false;
      wasBleMsgDisplayed = false;
      lastDisplayedCounter = 0xFFFFFFFF; 
    }
  } else if (counterUpdated) {
    // Clear the flag first so we don't miss/duplicate an update if the
    // ISR fires again while we're in the middle of handling this one.
    counterUpdated = false;

    // Only rewrite the LCD when the displayed value is actually stale,
    // to avoid unnecessary I2C traffic on every loop iteration.
    if (counter != lastDisplayedCounter) {
      char buffer[17];
      snprintf(buffer, sizeof(buffer), "Count: %lu", (unsigned long)counter);
      lcdPrint(buffer);
      lastDisplayedCounter = counter;
    }
  }
}