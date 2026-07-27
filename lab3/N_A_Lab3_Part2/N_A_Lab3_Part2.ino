/* Filename: N_A_Lab3_Part2
Author: Armina Moghadasi and Nathaniel Wagner
Date: 7/26/2026
Description: A TCB-driven task scheduler that manages non-blocking execution for an LED blinker, 
LCD counter, buzzer music player, and serial printer, adjusting task priorities upon each full completion cycle.*/

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
/* 
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

#define LED_BLINK_PIN 4
#define LED_MUSIC_PIN 5
#define BUZZER_PIN 6

#define LEDC_RESOLUTION 8 

#define NUM_TASKS 4

//construct LiquidCrystal and name it lcd
LiquidCrystal_I2C lcd(0x27, 16, 2);


//Task Control Blocks
//set states such that 0 -> ready, 1 -> running, 2 -> waiting, 3 -> completed
enum TaskState { ready, running, waiting, completed };
struct TCB;

// Function pointer signature taking a pointer to its own TCB
typedef void (*TaskFuncPtr)(TCB* me);

typedef struct TCB {
  const char* name;
  void (*taskFunction)(TCB* me); //pointer to function
  uint8_t priority; //priority level (1-4)
  TaskState state;
  unsigned long interval; //timing interval in milliseconds
  unsigned long lastRun; //time  of last execution
  uint16_t step; //step counter for task progress
  //led blinker toggles on and off 8 times, meaning 16 toggles/steps total for one cycle
  //alphabet printer means 26 steps for one cycle

} TCB;

void task0(TCB* me); //LED Blinker
void task1(TCB* me); //Counter
void task2(TCB* me); //Music Player
void task3(TCB* me); //Alphabet Printer

//storing tasks in an array
TCB tasks[NUM_TASKS] = {
    {"LED Blinker",      task0, 1, ready, 1000, 0, 0},
    {"LCD Counter",      task1, 2, ready, 1600, 0, 0},
    {"Music Player",     task2, 3, ready, 1700, 0, 0},
    {"Alphabet Printer", task3, 4, ready, 800, 0, 0}
};

//melody frequencies and voltage duty cycles
const uint16_t melody[10]  = {262, 294, 330, 349, 392, 440, 494, 523, 587, 659};
const uint8_t  voltages[10] = {25,  50,  75, 100, 125, 150, 175, 200, 225, 255};

//Task functions

//task Led Blinker
void task0(TCB* me) {
  if(me->step < 16) {
    bool ledState = (me->step % 2 == 0); //on if step is even, off if step is odd
    digitalWrite(LED_BLINK_PIN, ledState ? HIGH : LOW); //if ledState true output is HIGH
    me->step++;
  } else {
    digitalWrite(LED_BLINK_PIN, LOW);
    Serial.println("LED Blinker: " + String(me->priority));
    me->state = completed;
  }

}

//task Counter
void task1(TCB* me) {
  if(me->step < 10) {
    me->step++;

    lcd.setCursor(0,1);
    lcd.print("Count: ");
    lcd.print(me->step);
    lcd.print(" ");

    /*
    Serial.print("[Task 3] LCD Count: ");
    Serial.println(me->step);
    */
  } else {
    Serial.println("Counter: " + String(me->priority));
    me->state = completed;
  }
}

//task music player
void task2(TCB* me) {
  if(me->step < 10) {
    uint8_t idx = me->step;
    ledcAttach(BUZZER_PIN, melody[idx], LEDC_RESOLUTION); //configure buzzer PWN
    ledcWrite(BUZZER_PIN, 128); //play note at 50% cycle duty
    ledcWrite(LED_MUSIC_PIN, voltages[idx]); //output voltage brightness to LED 

    /*
    Serial.print("[Task 2] Playing Note ");
    Serial.print(idx + 1);
    Serial.print("/10 (Freq: ");
    Serial.print(melody[idx]);
    Serial.println(" Hz)");
    */
    me->step++;

  } else {
    ledcWrite(BUZZER_PIN, 0);
    ledcWrite(LED_MUSIC_PIN, 0);
    Serial.println("Music Player: " + String(me->priority));
    me->state = completed;
  }
}

//task alphabet
void task3(TCB* me) {
  if(me->step < 26) {
    char letter = 'A' + me->step;
    Serial.println(letter);
    Serial.print(" ");
    me->step++;
  } else {
    Serial.println("Alphabet Print: " + String(me->priority));
    me->state = completed;
  
  }
}


//order tasks by priority using bubble sort
void sortTasksByPriority() {
  for(int i = 0; i < NUM_TASKS - 1; i++) { //0->3
    for(int j = 0; j < NUM_TASKS - i - 1; j++) { 
      if (tasks[j].priority > tasks[j + 1].priority) {
                TCB temp = tasks[j];
                tasks[j] = tasks[j + 1];
                tasks[j + 1] = temp;
      }
    }
  }
}

bool areAllTasksCompleted() {
    for (int i = 0; i < NUM_TASKS; i++) {
        if (tasks[i].state != completed) return false;
    }
    return true;
}

void printTaskPriorities() {
    Serial.println("     CURRENT TASK PRIORITIES      ");
    for (int i = 0; i < NUM_TASKS; i++) {
        Serial.print("Priority ");
        Serial.print(tasks[i].priority);
        Serial.print(" -> ");
        Serial.println(tasks[i].name);
    }
}

void setup() {
  Serial.begin(115200);

    pinMode(LED_BLINK_PIN, OUTPUT);

    // Initialize LEDC channels
    ledcAttach(BUZZER_PIN, 2000, LEDC_RESOLUTION);
    ledcAttach(LED_MUSIC_PIN, 5000, LEDC_RESOLUTION);

    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Scheduler Active");

    sortTasksByPriority();

    // Align lastRun timestamps with setup time so tasks don't all trigger at once
    unsigned long startMillis = millis();
    for (int i = 0; i < NUM_TASKS; i++) {
        tasks[i].lastRun = startMillis;
    }

    printTaskPriorities();
}

void loop() {
  unsigned long curr_mills= millis();

  for(int i = 0; i < NUM_TASKS; i++) {
    if(tasks[i].state != completed) {
      if (curr_mills - tasks[i].lastRun >= tasks[i].interval) {
        tasks[i].state = running;
        tasks[i].lastRun = curr_mills;
        tasks[i].taskFunction(&tasks[i]);

        if (tasks[i].state != completed) {
          tasks[i].state = waiting;
        }
        break;
      }
    }
  }
  if (areAllTasksCompleted()) {
        Serial.println("All tasks completed");

        delay(4000); // 4 second pause so you can observe the finished state

        for (int i = 0; i < NUM_TASKS; i++) {
            tasks[i].priority = (tasks[i].priority % 4) + 1;
            tasks[i].step = 0;
            tasks[i].state = ready;
            tasks[i].lastRun = millis();
        }

        sortTasksByPriority();

        lcd.clear();
        lcd.setCursor(0, 0);

        printTaskPriorities();
    }
}
