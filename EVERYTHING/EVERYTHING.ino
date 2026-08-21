#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

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

    while (micros() - start < 5000) {
      int sample = analogRead(SOUND_PIN);
      if (sample < minValue) {
        minValue = sample;
      }
      if (sample > maxValue) {
        maxValue = sample;
      }
    }
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

const float USER_ENTER_DISTANCE = 50.0;
const float USER_EXIT_DISTANCE = 60.0;
bool userPresent = false;
bool previousUserPresent = false;
unsigned long welcomeStartTime = 0;
const unsigned long WELCOME_DURATION = 2000;

enum GameMode {
  WAITING_FOR_USER,
  PLAYING
};

GameMode gameMode = WAITING_FOR_USER;

void updateLCD(PetState state, float roomTemp) {
  lcd.clear();
  switch (state) {
    case HAPPY:
      lcd.setCursor(0, 0);
      lcd.print("Pet: HAPPY");
      lcd.setCursor(0, 1);
      lcd.print("Temp: ");
      lcd.print(roomTemp, 1);
      lcd.print("C");
      break;

    case HUNGRY:
      lcd.setCursor(0, 0);
      lcd.print("Pet: HUNGRY");
      lcd.setCursor(0, 1);
      lcd.print("Feed me!");
      break;

    case YAWNING:
      lcd.setCursor(0, 0);
      lcd.print("Pet: YAWNING");
      lcd.setCursor(0, 1);
      lcd.print("Getting sleepy");
      break;

    case SLEEPING:
      lcd.setCursor(0, 0);
      lcd.print("Pet: SLEEPING");
      lcd.setCursor(0, 1);
      lcd.print("Zzz...");
      break;

    case OVERSTIMULATED:
      lcd.setCursor(0, 0);
      lcd.print("Pet: UPSET");
      lcd.setCursor(0, 1);
      lcd.print("Too loud!");
      break;

    case HOT:
      lcd.setCursor(0, 0);
      lcd.print("Pet: HOT!");
      lcd.setCursor(0, 1);
      lcd.print("Room: ");
      lcd.print(roomTemp, 1);
      lcd.print("C");
      break;

    case COLD:
      lcd.setCursor(0, 0);
      lcd.print("Pet: COLD!");
      lcd.setCursor(0, 1);
      lcd.print("Room: ");
      lcd.print(roomTemp, 1);
      lcd.print("C");
      break;
  }
}

void GameTask(void *pvParameters) {
  const TickType_t taskPeriod = pdMS_TO_TICKS(100);
  TickType_t lastWakeTime = xTaskGetTickCount();

  SensorData data;
  PetState previousState = HAPPY;

  for(;;) {
    //recieve queue 1 from sensorTask
    if(xQueueReceive(sensorQueue, &data, 0) == pdTRUE) {

      //Checking if user is present
      if (!userPresent && data.distance > 0 && data.distance <= USER_ENTER_DISTANCE) {
        userPresent = true;
      }
      else if (userPresent && data.distance >= USER_EXIT_DISTANCE) {
        userPresent = false;
      }

      //waiting for user
      if (gameMode == WAITING_FOR_USER) {
        if (userPresent && !previousUserPresent) {
          gameMode = PLAYING;
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Welcome Back!");

          lcd.setCursor(0, 1);
          lcd.print("Tamagotchi!");

          welcomeStartTime = millis();

          Serial.println("User detected!");
          Serial.println("Game started.");
        }
      }
      else if(gameMode == PLAYING) {
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

        //update LCD only on state change
        if(millis() - welcomeStartTime >= WELCOME_DURATION) {
          if (petState != previousState) {
            updateLCD(petState, roomTemp);
            previousState = petState;
          }
        }

        //debug prints
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
      previousUserPresent = userPresent;
    }
    vTaskDelayUntil(&lastWakeTime, taskPeriod); 
  }
}

void setup() {
  Serial.begin(115200);

  //READ THIS:
  //Both DS3231 and the LCD use Wire.begin(SDA_PIN< SCL_PIN)
  //both are using the same I2C, so we should proetec with a mutex
  //before testing
  Wire.begin(SDA_PIN, SCL_PIN);

  if(!rtc.begin()) {
    Serial.print("RTC not found");
    while(1);
  }
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting the time!");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  //LCD
  lcd.init();
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tamagotchi");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

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
    2, //lower priority than sensorTask
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

