/* Filename: Lab4_Part1
Author: Armina Moghadasi and Nathaniel Wagner
Date: 08/10/2026
Description: This implements a algorithm that preempts tasks based on the 
shortest remaining time. The three tasks include LED Blinker, LCD counter, and 
alphabet printer. Based on which task has the shortest remaining execution time,
the scheduler preempts any current task that is not that task and then resumes 
the next task with the shortest exectution time until all tasks are complete. 
*/

/* 
info for me
The LiquidCrystal_I2C saves the 6-8 pins needed to wire a LCD directly to a microcontroller. Contains
commands that convert I2C bus commands into parallel signals that the LCD needs. 

constructor:
LiquidCrystal_I2C(address, cols, rows)

functions include:
lcd.init() //initalizes the I2C communication system
led.setCursor(col, row) //positions the text cursor at specific location
lcd.print("...") //writes character at current cursor location
lcd.backlight() // turns on the LCD background LED light
*/

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#define LED_BLINK_PIN 4
#define LEDC_RESOLUTION 8 
#define NUM_TASKS 3


//task handles
TaskHandle_t LEDTaskHandle = NULL;
TaskHandle_t CounterTaskHandle = NULL;
TaskHandle_t AlphabetTaskHandle = NULL;

// Total times for tasks
//portTick_PERIOD_MS is a constant macro that reperesents how many miliseconds elaspe during a 
//a single RTOS tick 
const TickType_t ledTaskExecutionTime = 500 / portTICK_PERIOD_MS;      // 500 ms
const TickType_t counterTaskExecutionTime = 2000 / portTICK_PERIOD_MS; // 2 seconds
const TickType_t alphabetTaskExecutionTime = 13000 / portTICK_PERIOD_MS; // 13 seconds

// Remaining Execution Times
volatile TickType_t remainingLedTime = ledTaskExecutionTime;
volatile TickType_t remainingCounterTime = counterTaskExecutionTime;
volatile TickType_t remainingAlphabetTime = alphabetTaskExecutionTime;

//construct LiquidCrystal and name it lcd
LiquidCrystal_I2C lcd(0x27, 16, 2);

//Task functions

//task Led Blinker: toggles the LED based on the amount of time left on the 
//execution time of the task. It accounts for the delay by subtracting it from
//remaining execution time. 
void task0(void *pvParameters) {
  Serial.println("LED started");
  const TickType_t stepDelay = pdMS_TO_TICKS(100);
  while(1) {
    if(remainingLedTime > 0) {
      digitalWrite(LED_BLINK_PIN, HIGH);
      vTaskDelay(stepDelay);
      digitalWrite(LED_BLINK_PIN, LOW);
      vTaskDelay(stepDelay);

      if(remainingLedTime >= (stepDelay)) {
        remainingLedTime -= (stepDelay);
      } else {
        remainingLedTime = 0;
      }
      if (remainingLedTime == 0) {
        Serial.println("LED done");
      }
    } else {
      vTaskDelay(stepDelay);
    }
  }
}

//task Counter: Prints from 1 to 20 on the LCD based on 
//the amount of time left on the execution time of the task. 
//It accounts for the delay by subtracting it from remaining execution time. 
void task1(void *pvParameters) {
  Serial.println("LCD started");
  const TickType_t stepDelay = pdMS_TO_TICKS(100);
  for(;;) {
    if(remainingCounterTime > 0) {
      for(int i = 1; i <= 20; i++) {
        lcd.setCursor(0, 1);
        lcd.print("COUNT: ");
        lcd.print(i);
        lcd.print(" ");
        vTaskDelay(stepDelay);

        if(remainingCounterTime >= stepDelay) {
          remainingCounterTime -= stepDelay;
        } else {
          remainingCounterTime = 0;
        }
      }
      if (remainingLedTime == 0) {
        Serial.println("LCD done");
      }
    } else {
      vTaskDelay(stepDelay);
    }
  }
}

//task alphabet: prints the alphabet from A to Z based on
//execution time and accounts for delays similarly to the other tasks.
void task2(void *pvParameters) {
    Serial.println("alphabet started");
    const TickType_t stepDelay = pdMS_TO_TICKS(500);

    for(;;) {
      if(remainingAlphabetTime > 0) {
        for(int i = 0; i < 26; i++) {
          if (remainingAlphabetTime == 0) break;

          char letter = 'A' + i;
          Serial.println(letter);

          vTaskDelay(500 / portTICK_PERIOD_MS);
      
          if(remainingAlphabetTime >= stepDelay) {
            remainingAlphabetTime -= stepDelay;
          } else {
            remainingAlphabetTime = 0;
          }
        }
        if (remainingAlphabetTime == 0) {
          Serial.println("alphabet done");
        }
    } else {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

///SRFT scheduler task has the highest prioirty and runs continuously 
//to manage three worker tasks (LED, Counter, Alphabet).
//it tracks the shortest task and the minTime, comparing each task's remaining
//execution with minTime and setting it to shortestTask if it is smaller.
//It suspends tasks if they are not the shortestTask and resumes the execution
//time of the shortestTask.
//It evaluates which task has the smallest remaining execution time, suspends all other tasks,
//and resumes the task with the shortest remaining time.

void srtfSchedulerTask(void *pvParameters) {
  TickType_t checkPeriod = 50 / portTICK_PERIOD_MS;
  int timeElapsed = 0;
  
  for(;;) {
    // Inside srtfSchedulerTask:
    timeElapsed += 50;
    
    // Reset LED time at 5 seconds (5000 ms) while Alphabet Task is printing
    if (timeElapsed == 5000) {
      Serial.println("\n>>> PREEMPTION TRIGGERED: LED Task inserted! <<<");
      remainingLedTime = pdMS_TO_TICKS(400); // 400 ms blink
    }

    //initalizing minTime and the shortest task
    TickType_t minTime = portMAX_DELAY;
    TaskHandle_t shortestTask = NULL;

    //updating shortest task and minTime based on completion or when less than minTime
    if(remainingLedTime > 0 && remainingLedTime < minTime) {
      minTime = remainingLedTime;
      shortestTask = LEDTaskHandle;
    }
    if (remainingCounterTime > 0 && remainingCounterTime < minTime) {
      minTime = remainingCounterTime;
      shortestTask = CounterTaskHandle;
    }
    if (remainingAlphabetTime > 0 && remainingAlphabetTime < minTime) {
      minTime = remainingAlphabetTime;
      shortestTask = AlphabetTaskHandle;
    }

    //preemptive scheduling
    if(shortestTask != NULL) {
      if(shortestTask != LEDTaskHandle && LEDTaskHandle != NULL) {
        vTaskSuspend(LEDTaskHandle);
      } 
      if(shortestTask != CounterTaskHandle && CounterTaskHandle != NULL) {
        vTaskSuspend(CounterTaskHandle);
      } 
      if(shortestTask != AlphabetTaskHandle && AlphabetTaskHandle != NULL){
        vTaskSuspend(AlphabetTaskHandle);
      }

      vTaskResume(shortestTask);
    } else {
      Serial.println("All tasks completed");
    }
    
    vTaskDelay(checkPeriod);
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(17, 18);
  pinMode(LED_BLINK_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Scheduler Active");

  xTaskCreatePinnedToCore(task0, "LED Task", 2048, NULL, 1, &LEDTaskHandle, 0);
  xTaskCreatePinnedToCore(task1, "Counter Task", 2048, NULL, 1, &CounterTaskHandle, 0);
  xTaskCreatePinnedToCore(task2, "Alphabet Task", 2048, NULL, 1, &AlphabetTaskHandle, 0);

  vTaskSuspend(LEDTaskHandle);
  vTaskSuspend(CounterTaskHandle);
  vTaskSuspend(AlphabetTaskHandle);

  xTaskCreatePinnedToCore(srtfSchedulerTask, "SRTF Scheduler", 2048, NULL, 5, NULL, 0);
}

void loop() {
}