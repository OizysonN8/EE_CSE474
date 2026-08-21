#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <DHT.h>

#define SOUND_PIN 2
#define ADC_MAX 4095

#define TRIG_PIN 5
#define ECHO_PIN 4
#define SDA_PIN 6
#define SCL_PIN 7
#define DHT_PIN 8
#define DHT_TYPE DHT11

RTC_DS3231 rtc;
DHT dht(DHT_PIN, DHT_TYPE);
int minVal = ADC_MAX;
int maxVal = 0;

//call function once
float readDistance();


//roomTemp
const float COMFORT_MIN = 18.0;
const float COMFORT_MAX = 26.0;

//sound threshold 
const int SOUND_THRESHOLD = 250;
volatile int latestSoundLevel = 0;

void SoundTask(void *pvParameters) {
  const TickType_t taskPeriod = pdMS_TO_TICKS(10);
  TickType_t lastWakeTime = xTaskGetTickCount();
  
  for (;;) {
    int minValue = 4095;
    int maxValue = 0;
    unsigned long start = micros();

    // Collect samples for 5 ms
    while (micros() - start < 5000) {
      int sample = analogRead(SOUND_PIN);
      if (sample < minValue) {
        minValue = sample;
      }

      if (sample > maxValue) {
        maxValue = sample;
      }
    }
    // Peak-to-peak amplitude
    latestSoundLevel = maxValue - minValue;
    vTaskDelayUntil(&lastWakeTime, taskPeriod);
  }
}

//Sensor Task
struct SensorData {
  float temp;
  float humidity;
  int soundLevel;
  float distance;

  int hour;
  int minute;
  int second;
};

//set up queue #1: from sensor to game
QueueHandle_t sensorQueue;

float roomTemp = 22; //temporary until real readings

//Sensor Task
void SensorTask(void *pvParameters) {
  const TickType_t taskPeriod = pdMS_TO_TICKS(10);
  TickType_t lastWakeTime = xTaskGetTickCount();
  SensorData data;

  //counters/
  int dhtCounter = 100;
  bool readSonar = false;
  int rtcCounter = 100;

  for(;;) {
    //Read humidity and temperature
    if(dhtCounter >= 100) {
      data.temp = dht.readTemperature();
      data.humidity = dht.readHumidity();

      dhtCounter = 0;
    }

    //readSonar
    if(readSonar) {
      data.distance = readDistance();
    }

    readSonar = !readSonar;

     //read soundlevel
    data.soundLevel = latestSoundLevel;

    //RTC readings

    if(rtcCounter >= 100) {
      DateTime now = rtc.now();
      data.hour = now.hour();
      data.minute = now.minute();
      data.second = now.second();
      rtcCounter = 0;
    }

    xQueueOverwrite(sensorQueue, &data);

    //update counters/trackers
    dhtCounter++;
    rtcCounter++;
    // Run again after 10 ms
    vTaskDelayUntil(&lastWakeTime, taskPeriod);
  }
}

float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000); //watch out for this, might get blocked

  if (duration == 0) {
      return -1.0;
  }
  return duration / 58.0;
}

int readSoundLevel() {
  int minValue = 4095;
  int maxValue = 0;

  unsigned long start = millis();

  while (millis() - start < 50) {

    int sample = analogRead(SOUND_PIN);

    if (sample < minValue)
      minValue = sample;

    if (sample > maxValue)
      maxValue = sample;
  }

  return maxValue - minValue;
}

//PetState
enum PetState {
  HAPPY,
  HUNGRY,
  YAWNING,
  SLEEPING, 
  OVERSTIMULATED, //too much sound ,
  HOT,
  COLD
};

PetState petState = HAPPY;
int hunger = 100;

void GameTask(void *pvParameters) {
  const TickType_t taskPeriod = pdMS_TO_TICKS(100);
  TickType_t lastWakeTime = xTaskGetTickCount();

  SensorData data;

  for(;;) {
    //recieve queue 1 from sensorTask
    if(xQueueReceive(sensorQueue, &data, 0) == pdTRUE) {
      int hour = data.hour;

      //sleeping hours
      if(hour >= 22 || hour < 7) {
        petState = SLEEPING;
      }
      //too much sound/noise
      else if (data.soundLevel > SOUND_THRESHOLD) {
        petState = OVERSTIMULATED;
      }
      else if (roomTemp > COMFORT_MAX) {
          petState = HOT;
      }
      else if (roomTemp < COMFORT_MIN) {
          petState = COLD;
      }
      else if (hunger < 30) {
        petState = HUNGRY;
      }
      else {
        petState = HAPPY;
      }

      //debug
      Serial.print("GameTask | State: ");

      switch (petState) {
        case HAPPY:
          Serial.println("HAPPY");
          break;

        case HUNGRY:
          Serial.println("HUNGRY");
          break;

        case YAWNING:
          Serial.println("YAWNING");
          break;

        case SLEEPING:
          Serial.println("SLEEPING");
          break;

        case OVERSTIMULATED:
          Serial.println("OVERSTIMULATED");
          break;

        case HOT:
          Serial.println("HOT");
          break;

        case COLD:
          Serial.println("COLD");
          break;
      }
    }

    vTaskDelayUntil(&lastWakeTime, taskPeriod); 
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);

  if(!rtc.begin()) {
    Serial.print("RTC not found");
    while(1);
  }
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting the time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  analogReadResolution(12);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  dht.begin();

  sensorQueue = xQueueCreate(1, sizeof(SensorData));

  if(sensorQueue == NULL) {
    Serial.println("Failed to create queue");
    while(1);
  }

  xTaskCreatePinnedToCore(
    SensorTask,          
    "SensorTask",        
    4096,                
    NULL,                
    3,                   
    NULL,                
    0                    
  );

  xTaskCreatePinnedToCore(
    SoundTask,
    "SoundTask",
    4096,
    NULL,
    3,
    NULL,
    0
  );

  xTaskCreatePinnedToCore(
    GameTask,
    "GameTask",
    4096,
    NULL,
    2,
    NULL,
    1
  );
}

void loop() {

}