/**
 * @file N_A_ICTE8.ino
 * @author Nathaniel Wagner and Armina Moghadasi
 * @brief A traffic light controller that illuminates three LEDs external to an
 *        ESP32S3 in sequential order, each for 3 seconds at a time.
 * @details This program uses FreeRTOS and a simple FSM to execute the tasks in
 *          a non-blocking, priority driven manner.
 * @date 2026-08-05
 */

#include "freertos/FreeRTOS.h"     // FreeRTOS kernel API
#include "freertos/task.h"         // Task creation/management
#include "freertos/queue.h"        // Queue for inter-task communication

// ==============================================
// Pin Definitions
// ==============================================
#define RED_LED 2
#define YELLOW_LED 5
#define GREEN_LED 4

// The amount of stack space to give each task (in words)
#define STACK_ALLOC_TO_EACH_TASK 2048
#define QUEUE_SIZE 2

// ==============================================
// Queue Handle
// ==============================================
QueueHandle_t state_queue; // Passes traffic light states between tasks.


// ==============================================
// Enum for Traffic Light States
// ==============================================
enum State {RED, GREEN, YELLOW};


// ==============================================
// Traffic Controller Task
// ==============================================
/**
 * @brief FSM that changes between states RED, YELLOW, AND GREEN sequentially.
 * 
 * @param pvParameters Unused parameter port.
 */
void Task_TrafficController(void *pvParameters) {
    while(1) {
        State red = RED;
        State yellow = YELLOW;
        State green = GREEN;
        // xQueueSend needs a pointer argument for the element to add.
        xQueueSend(state_queue, &red, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(3000));
        xQueueSend(state_queue, &green, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(3000));
        xQueueSend(state_queue, &yellow, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}


// ==============================================
// Traffic Light Task
// ==============================================
/**
 * @brief Illuminates the correct LED based on the state of the FSM.
 * 
 * @param pvParameters Unused parameter port.
 */
void Task_TrafficLight(void *pvParameters) {
    State curr_state;
    while (1) {
        if (xQueueReceive(state_queue, &curr_state, pdMS_TO_TICKS(100)) == pdTRUE) {
            digitalWrite(RED_LED, LOW);
            digitalWrite(GREEN_LED, LOW);
            digitalWrite(YELLOW_LED, LOW);

            if (curr_state == GREEN) {
                Serial.println("Traffic Light: GREEN (Go)");
                digitalWrite(GREEN_LED, HIGH);
            } else if (curr_state == RED) {
                Serial.println("Traffic Light: RED (Stop)");
                digitalWrite(RED_LED, HIGH);
            } else if (curr_state == YELLOW) {
                Serial.println("Traffic Light: YELLOW (Slow Down)");
                digitalWrite(YELLOW_LED, HIGH);
            }
        }
    }
}


// ==============================================
// Setup
// ==============================================
void setup() {
    Serial.begin(115200);

    pinMode(RED_LED, OUTPUT);
    pinMode(GREEN_LED, OUTPUT);
    pinMode(YELLOW_LED, OUTPUT);

    state_queue = xQueueCreate(QUEUE_SIZE, sizeof(enum State));
    if (state_queue == NULL) {
        Serial.println("Error: Queue not created!");
        while (1); // Halt if queue creation fails
    }

    // Give both tasks the same priority
    xTaskCreate(Task_TrafficController, "Controller", STACK_ALLOC_TO_EACH_TASK, NULL, 2, NULL);
    xTaskCreate(Task_TrafficLight, "Lights",  STACK_ALLOC_TO_EACH_TASK, NULL, 1, NULL);

    Serial.println("System started.");
}


// ==============================================
// Loop
// ==============================================
void loop() {
    // Leave empty. FreeRTOS handles task scheduling.
}