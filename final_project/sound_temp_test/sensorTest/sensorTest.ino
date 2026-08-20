/*
 * ESP32-S3 FreeRTOS sensor -> LCD system
 * Board: ESP32-S3, Arduino IDE 2.3.10, ESP32 Arduino core 3.3.11
 *
 * Wiring:
 *   LCD (PCF8574T, addr 0x27): SDA -> GPIO17, SCL -> GPIO18
 *   DHT11 (4-pin, 10k pull-up DATA->VCC installed): DATA -> GPIO5
 *   Sound sensor AO (continuous level): -> GPIO2 (ADC1_CH1)
 *
 * Libraries required (Library Manager):
 *   - DHT sensor library (Adafruit) + Adafruit Unified Sensor
 *   - LiquidCrystal_I2C (or LiquidCrystal_PCF8574)
 *
 * Design:
 *   - TaskHumidity and TaskSound each own their sensor, write results into a
 *     mutex-protected shared struct, then notify TaskLCD.
 *   - TaskLCD blocks on notification, copies the struct under the mutex, and
 *     redraws. This avoids torn/partial reads if both tasks fire close together.
 *
 * Debug note: some DHT library builds return 0.0 (not NaN) on a failed/checksum
 * read. We treat an exact 0.0 on either field as invalid too, since real
 * humidity is never exactly 0% and a stuck 0C reading is almost always a
 * misread, not a true value.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

#include "esp_system.h"  // TODO: REMOVE

#define LCD_ADDR      0x27
#define LCD_COLS      16
#define LCD_ROWS      2
#define SDA_PIN       17
#define SCL_PIN       18

#define DHT_PIN       5
#define DHT_TYPE      DHT11

#define SOUND_PIN     2   // ADC1_CH1, safe on ESP32-S3 (avoid GPIO6-11: internal flash)
#define ADC_MAX       4095

#define DHT_PERIOD_MS      2000  // DHT11 needs >=1s between reads; 2s gives margin
#define DHT_WARMUP_MS      2000  // let sensor stabilize before first read
#define SOUND_PERIOD_MS     200  // continuous-feeling sound level updates
#define SOUND_DEADBAND        2  // % change required before triggering an LCD update

LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
DHT dht(DHT_PIN, DHT_TYPE);

TaskHandle_t lcdTaskHandle = NULL;
TaskHandle_t soundTaskHandle = NULL;
TaskHandle_t humidityTaskHandle = NULL;

typedef struct {
  float humidity;
  float tempC;
  bool  dhtValid;
  int   soundPct;
} SensorData_t;

SensorData_t sharedData = { 0.0f, 0.0f, false, 0 };
SemaphoreHandle_t dataMutex;

#define NOTIFY_HUMIDITY_BIT (1 << 0)
#define NOTIFY_SOUND_BIT    (1 << 1)

void TaskHumidity(void *pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(DHT_WARMUP_MS));
  static int loopCount = 0;

  for (;;) {
    vTaskPrioritySet(NULL, 3);
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    vTaskPrioritySet(NULL, 1);

    bool valid = !isnan(h) && !isnan(t) && h != 0.0f && t != 0.0f;
    Serial.printf("[DHT] raw h=%.2f t=%.2f valid=%d\n", h, t, valid);

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      sharedData.dhtValid = valid;
      if (valid) {
        sharedData.humidity = h;
        sharedData.tempC = t;
      }
      xSemaphoreGive(dataMutex);
    }

    xTaskNotify(lcdTaskHandle, NOTIFY_HUMIDITY_BIT, eSetBits);

    if (++loopCount % 5 == 0) {
      Serial.printf("[STACK] LCD=%u Humidity=%u Sound=%u\n",
        uxTaskGetStackHighWaterMark(lcdTaskHandle),
        uxTaskGetStackHighWaterMark(NULL),
        uxTaskGetStackHighWaterMark(soundTaskHandle));
    }

    vTaskDelay(pdMS_TO_TICKS(DHT_PERIOD_MS));
  }
}

void TaskSound(void *pvParameters) {
  int lastPct = -1;

  for (;;) {
    int raw = analogRead(SOUND_PIN);
    int pct = (raw * 100) / ADC_MAX;

    Serial.printf("[SND] raw=%d pct=%d\n", raw, pct);

    if (abs(pct - lastPct) >= SOUND_DEADBAND) {
      lastPct = pct;

      if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        sharedData.soundPct = pct;
        xSemaphoreGive(dataMutex);
      }

      xTaskNotify(lcdTaskHandle, NOTIFY_SOUND_BIT, eSetBits);
    }

    vTaskDelay(pdMS_TO_TICKS(SOUND_PERIOD_MS));
  }
}

void TaskLCD(void *pvParameters) {
  SensorData_t localCopy;
  uint32_t notifyBits;
  char line0[LCD_COLS + 1];
  char line1[LCD_COLS + 1];

  for (;;) {
    xTaskNotifyWait(0, ULONG_MAX, &notifyBits, portMAX_DELAY);

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
      localCopy = sharedData;
      xSemaphoreGive(dataMutex);
    }

    if (localCopy.dhtValid) {
      snprintf(line0, sizeof(line0), "T:%.1fC H:%.0f%% ", localCopy.tempC, localCopy.humidity);
    } else {
      snprintf(line0, sizeof(line0), "DHT read error  ");
    }
    snprintf(line1, sizeof(line1), "Sound: %3d%%    ", localCopy.soundPct);

    lcd.setCursor(0, 0);
    lcd.print(line0);

    lcd.setCursor(0, 1);
    lcd.print(line1);
  }
}

void setup() {
  Serial.begin(115200);

  delay(200);
  esp_reset_reason_t r = esp_reset_reason();
  Serial.printf("Reset reason: %d\n", r);  // ESP_RST_BROWNOUT = 15, 

  Wire.begin(SDA_PIN, SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.clear();

  dht.begin();
  analogReadResolution(12);

  dataMutex = xSemaphoreCreateMutex();

  xTaskCreate(TaskLCD, "TaskLCD", 8192, NULL, 2, &lcdTaskHandle);
  xTaskCreate(TaskHumidity, "TaskHumidity", 8192, NULL, 1, &humidityTaskHandle);
  xTaskCreate(TaskSound, "TaskSound", 8192, NULL, 1, &soundTaskHandle);
}

void loop() {
  // Empty: all work happens in FreeRTOS tasks
}