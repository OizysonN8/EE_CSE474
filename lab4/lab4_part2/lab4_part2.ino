/**
* @file lab4_part2.ino
* @author Armina Moghadasi and Nate Wagner
* @date 08/10/2026
* @brief This file implements a dual-core light sensor and anomaly 
* detection system using FreeRTOS. It pins two tasks to core 0, the light
* detecter (which reads from a photoresister and calculates the SMA which is
* the average of the last 5 readings) and the Light Level Display which the 
* light level data and SMA data to be displayed on the LCD. The next two tasks
* are pined to core 1, which includes the Anomaly Alarm which toggles the LED
* when the environment is too dark or too bright and the Prime Calculation task
* that repeatedly calculates all the prime numbers from 2 to 5000. 
*/

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#define ANALOG_PIN 1
#define LED_PIN 4

volatile int rawLightVal = 0;
volatile float currentSMA = 0.0;


SemaphoreHandle_t xLightSemaphore;
LiquidCrystal_I2C lcd(0x27, 16, 2);
bool isPrime(int n);

/**
* @brief: Initialize Variables, Loop Continuously: Read light level 
* from the photoresistor Take semaphore, Calculate the simple moving 
* average and update variables. Give semaphore to signal data is ready.
*/
void LightDetectorTask(void *pvParameters) {
  const int size = 5;
  int rawHistory[size] = {0}; //circular buffer array
  int historyIdx = 0;
  int sum = 0;
  
  for(;;) {
    int new_val = analogRead(ANALOG_PIN);
    sum  -= rawHistory[historyIdx]; //replace old reading
    rawHistory[historyIdx] = new_val;
    sum += new_val;
    historyIdx = (historyIdx + 1) % size;

    rawLightVal = new_val;
    currentSMA = (float)sum / size; //update average

    xSemaphoreGive(xLightSemaphore);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}


/**
* @brief Initialize Variables. Loop Continuously: Wait for semaphore, If data has changed, 
* update the LCD with the new light level and SMA. Give back the semaphore.
*/
void LCDTask(void *pvParameters) {
  int lastRawVal =-999;
  float lastSMAVal = -999.0;

  for(;;) {
    if(xSemaphoreTake(xLightSemaphore, portMAX_DELAY) == pdTRUE) {
      int currentRaw = rawLightVal;
      int currentSMAInt = (int)currentSMA;

      if(abs(currentRaw - lastRawVal) > 30 || abs(currentSMAInt - lastSMAVal) > 1.0){
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Light: ");
        lcd.print(currentRaw);
        lcd.print("    ");

        lcd.setCursor(0,1);
        lcd.print("SMA: ");
        lcd.print(currentSMA, 1);
        lcd.print("    ");

        lastRawVal = currentRaw;
        lastSMAVal = currentSMAInt;
      } 
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

/**
* @brief Loop Continuously Wait for semaphore, Check if SMA indicates a light 
* anomaly (outside thresholds), If an anomaly is detected, flash a LED signal.
* Give back the semaphore.
*/
void AnomalyAlarmTask(void *pvParameters) {
  const float HIGH_THRESHOLD = 3800.0;
  const float LOW_THRESHOLD = 2500.0;
  for(;;) {
    float sma = currentSMA;
    if (sma > HIGH_THRESHOLD || sma < LOW_THRESHOLD) {
      Serial.println("ANOMALY DETECTED! Flashing LED...");
      for (int i = 0; i < 3; i++) {
        digitalWrite(LED_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(150)); // LED ON duration
        digitalWrite(LED_PIN, LOW);
        vTaskDelay(pdMS_TO_TICKS(150)); // LED OFF duration
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }
}

/**
* @brief Loop from 2 to 5000, Check if the current number is prime, 
* If prime, print the number to the serial monitor.
*/
void PrimeCalculationTask(void *pvParameters) {
  for(;;) {
    for (int i = 2; i <= 5000; i++) {
      if (isPrime(i)) {
        Serial.print("Prime found: ");
        Serial.println(i);
      }
      //Small delay every few iterations to prevent starving the task 
      // scheduler/watchdog timer on Core 1
      if (i % 100 == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

bool isPrime(int n) {
  if (n <= 1) return false;
  if (n <= 3) return true;
  if (n % 2 == 0 || n % 3 == 0) return false;

  for (int i = 5; i * i <= n; i += 6) {
    if (n % i == 0 || n % (i + 2) == 0) {
      return false;
    }
  }
  return true;
}

void setup() {
  //         1. Initialize pins, serial, LCD, etc
  Serial.begin(115200);
  Wire.begin(17,18);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  pinMode(LED_PIN, OUTPUT);
  pinMode(ANALOG_PIN, INPUT);

  //         2. Create binary semaphore for synchronization of light level data.
  xLightSemaphore = xSemaphoreCreateBinary();
  //         3. Create Tasks
  //          - Create the `Light Detector Task` and assign it to Core 0.
  xTaskCreatePinnedToCore(LightDetectorTask, "Light Detector Task", 2048, NULL, 2, NULL, 0);
  //          - Create `LCD Task` and assign it to Core 0.
  xTaskCreatePinnedToCore(LCDTask,  "LCD Task",  2048, NULL, 1, NULL, 0);
  //          - Create `Anomaly Alarm Task` and assign it to Core 1.
  xTaskCreatePinnedToCore(AnomalyAlarmTask,  "Anomaly Alarm Task",  2048, NULL, 2, NULL, 1);
  //          - Create `Prime Calculation Task` and assign it to Core 1.
  xTaskCreatePinnedToCore(PrimeCalculationTask, "Prime Calculation Task", 2048, NULL, 1, NULL, 1);
}

void loop() {
  //empty
}


