/* ============================================================================
 * CSE/EE 474 Final Project — Input-Only RTOS Test Harness
 * Target: ESP32-S3 (Arduino framework, FreeRTOS)
 *
 * PURPOSE
 *   Exercises the three physical inputs (debounced pushbutton, KY-022 IR
 *   receiver, potentiometer) against a simplified pet-state model, with NO
 *   real sensor input. Temperature is a fixed placeholder so the comfort-band
 *   logic can be validated by turning the pot alone.
 *
 * ARCHITECTURE
 *   InputTask   Core 1, 100 Hz, prio 3 -- polls button + pot, consumes IR ISR
 *   GameTask    Core 1,  10 Hz, prio 2 -- sole owner of PetState
 *   ReportTask  Core 1,   1 Hz, prio 1 -- prints periodic PetState heartbeat
 *
 *   Queue #2 (qEvents)   : discrete InputEvent, InputTask/timers -> GameTask
 *   Pot queue (qComfort) : 1-element overwrite, latest comfort center only
 *   State queue (qStateOut): 1-element overwrite, latest PetState snapshot,
 *                            GameTask -> ReportTask
 *
 *   Timer #2 (tHunger)  : 30 s auto-reload -> re-asserts HUNGRY
 *   Timer #3 (tStim)    : 20 s auto-reload -> re-asserts OVERSTIMULATED
 *
 * OWNERSHIP RULE
 *   Only GameTask writes PetState. Timers and InputTask never touch it; they
 *   only post events. ReportTask never touches PetState directly either --
 *   it only reads a snapshot copy that GameTask publishes each cycle. This
 *   removes the need for any mutex on the state struct.
 * ========================================================================== */


#include <Arduino.h>


/* ---------------------------- Pin map ----------------------------------- */
static const uint8_t PIN_BTN_FEED = 15;   // to GND, internal pull-up
static const uint8_t PIN_IR_CALM  = 16;   // KY-022 signal pin, idles HIGH
static const uint8_t PIN_POT      = 1;    // ADC1_CH0 (Wi-Fi safe)


/* ------------------------ Simulated temperature ------------------------- */
static const float SIM_TEMP_C   = 23.0f;  // placeholder for DHT11
static const float BAND_WIDTH_C = 3.0f;   // comfort spread
static const float BAND_HALF_C  = BAND_WIDTH_C / 2.0f;
static const float HYST_C       = 0.5f;   // edge hysteresis
static const float CENTER_MIN_C = 15.0f;  // pot fully CCW
static const float CENTER_MAX_C = 30.0f;  // pot fully CW


/* ---------------------------- Task timing ------------------------------- */
static const TickType_t INPUT_PERIOD  = pdMS_TO_TICKS(10);    // 100 Hz
static const TickType_t GAME_PERIOD   = pdMS_TO_TICKS(100);   //  10 Hz
static const TickType_t REPORT_PERIOD = pdMS_TO_TICKS(1000);  //   1 Hz


/* ----------------------------- Needs timers ----------------------------- */
static const uint32_t HUNGER_PERIOD_MS = 30000;
static const uint32_t STIM_PERIOD_MS   = 20000;


/* ------------------------------ Debounce -------------------------------- */
static const uint32_t BTN_DEBOUNCE_MS = 50;   // stable window
static const uint32_t IR_LOCKOUT_MS   = 200;  // rejects NEC repeat frames


/* ------------------------------- Types ---------------------------------- */
typedef enum {
  EVT_FEED = 0,
  EVT_CALM,
  EVT_HUNGER_EXPIRE,
  EVT_STIM_EXPIRE
} EventType;


typedef struct {
  EventType type;
  uint32_t  t_ms;
} InputEvent;


typedef struct {
  bool hot;
  bool cold;
  bool overstimulated;
  bool hungry;
} PetState;


/* ------------------------------ Globals --------------------------------- */
static QueueHandle_t qEvents   = NULL;
static QueueHandle_t qComfort  = NULL;
static QueueHandle_t qStateOut = NULL;   // 1-element overwrite, GameTask -> ReportTask


static TimerHandle_t tHunger = NULL;
static TimerHandle_t tStim   = NULL;


static volatile uint32_t g_irEdges = 0;   // bumped by ISR only


static volatile uint32_t g_inputCycles = 0;
static volatile uint32_t g_gameCycles  = 0;


/* -------------------------------- ISR ----------------------------------- */
static void IRAM_ATTR irIsr() {
  g_irEdges++;
}


/* --------------------------- Timer callbacks ---------------------------- */
static void hungerTimerCb(TimerHandle_t) {
  InputEvent e = { EVT_HUNGER_EXPIRE, millis() };
  xQueueSend(qEvents, &e, 0);
}


static void stimTimerCb(TimerHandle_t) {
  InputEvent e = { EVT_STIM_EXPIRE, millis() };
  xQueueSend(qEvents, &e, 0);
}


/* ------------------------------ InputTask ------------------------------- */
static void InputTask(void *) {
  TickType_t lastWake = xTaskGetTickCount();

  int      btnRaw        = HIGH;
  int      btnStable     = HIGH;
  uint32_t btnLastChange = 0;

  uint32_t irSeen        = 0;
  uint32_t irLastAccept  = 0;

  float potEma = (float)analogRead(PIN_POT);

  for (;;) {
    const uint32_t now = millis();

    const int r = digitalRead(PIN_BTN_FEED);
    if (r != btnRaw) {
      btnRaw = r;
      btnLastChange = now;
    } else if (btnRaw != btnStable && (now - btnLastChange) >= BTN_DEBOUNCE_MS) {
      btnStable = btnRaw;
      if (btnStable == LOW) {
        InputEvent e = { EVT_FEED, now };
        xQueueSend(qEvents, &e, 0);
      }
    }

    const uint32_t edges = g_irEdges;
    if (edges != irSeen) {
      irSeen = edges;
      if ((now - irLastAccept) >= IR_LOCKOUT_MS) {
        irLastAccept = now;
        InputEvent e = { EVT_CALM, now };
        xQueueSend(qEvents, &e, 0);
      }
    }

    potEma = (potEma * 0.9f) + ((float)analogRead(PIN_POT) * 0.1f);
    float center = CENTER_MIN_C +
                   (potEma / 4095.0f) * (CENTER_MAX_C - CENTER_MIN_C);
    xQueueOverwrite(qComfort, &center);

    g_inputCycles++;
    vTaskDelayUntil(&lastWake, INPUT_PERIOD);
  }
}


/* ------------------------------- GameTask ------------------------------- */
static void GameTask(void *) {
  TickType_t lastWake = xTaskGetTickCount();

  PetState st = { false, false, true, true };
  PetState prev;
  bool     firstPrint = true;

  float center = (CENTER_MIN_C + CENTER_MAX_C) / 2.0f;
  uint32_t lastStats = millis();

  for (;;) {
    InputEvent e;
    while (xQueueReceive(qEvents, &e, 0) == pdTRUE) {
      switch (e.type) {
        case EVT_FEED:
          st.hungry = false;
          xTimerReset(tHunger, 0);
          Serial.println("[EVT] FEED   -> hunger cleared, Timer #2 reset");
          break;
        case EVT_CALM:
          st.overstimulated = false;
          xTimerReset(tStim, 0);
          Serial.println("[EVT] CALM   -> overstim cleared, Timer #3 reset");
          break;
        case EVT_HUNGER_EXPIRE:
          st.hungry = true;
          break;
        case EVT_STIM_EXPIRE:
          st.overstimulated = true;
          break;
      }
    }

    xQueueReceive(qComfort, &center, 0);

    const float hi = center + BAND_HALF_C;
    const float lo = center - BAND_HALF_C;

    if (st.hot) {
      st.hot = (SIM_TEMP_C > (hi - HYST_C));
    } else {
      st.hot = (SIM_TEMP_C > hi);
    }

    if (st.cold) {
      st.cold = (SIM_TEMP_C < (lo + HYST_C));
    } else {
      st.cold = (SIM_TEMP_C < lo);
    }

    if (st.hot && st.cold) {
      st.cold = false;
    }

    if (firstPrint ||
        st.hot != prev.hot || st.cold != prev.cold ||
        st.overstimulated != prev.overstimulated ||
        st.hungry != prev.hungry) {
      Serial.printf(
        "[STATE] band %.1f-%.1f C (center %.1f, temp %.1f) | "
        "HOT=%d COLD=%d OVERSTIM=%d HUNGRY=%d\n",
        lo, hi, center, SIM_TEMP_C,
        st.hot, st.cold, st.overstimulated, st.hungry);
      prev = st;
      firstPrint = false;
    }

    /* Publish snapshot for ReportTask (unconditional, every cycle) */
    xQueueOverwrite(qStateOut, &st);

    const uint32_t now = millis();
    if ((now - lastStats) >= 5000) {
      const uint32_t ic = g_inputCycles, gc = g_gameCycles;
      g_inputCycles = 0; g_gameCycles = 0;
      Serial.printf("[RATE] InputTask %.1f Hz  GameTask %.1f Hz\n",
                    ic * 1000.0f / (now - lastStats),
                    gc * 1000.0f / (now - lastStats));
      lastStats = now;
    }

    g_gameCycles++;
    vTaskDelayUntil(&lastWake, GAME_PERIOD);
  }
}


/* ------------------------------ ReportTask ------------------------------- */
/* Lowest priority: purely cosmetic periodic status dump. Never touches
   PetState directly -- reads a GameTask-published snapshot only, so no
   mutex or cross-task lock is required. Uses xQueuePeek (not xQueueReceive)
   so a stale-but-valid snapshot always remains available. */
static void ReportTask(void *) {
  PetState snap;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    if (xQueuePeek(qStateOut, &snap, 0) == pdTRUE) {
      Serial.printf(
        "[TICK] HOT=%d COLD=%d OVERSTIM=%d HUNGRY=%d\n",
        snap.hot, snap.cold, snap.overstimulated, snap.hungry);
    }
    vTaskDelayUntil(&lastWake, REPORT_PERIOD);
  }
}


/* -------------------------------- setup --------------------------------- */
void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Input-Only RTOS Test Harness ===");

  pinMode(PIN_BTN_FEED, INPUT_PULLUP);
  pinMode(PIN_IR_CALM,  INPUT);
  analogReadResolution(12);

  qEvents   = xQueueCreate(8, sizeof(InputEvent));
  qComfort  = xQueueCreate(1, sizeof(float));
  qStateOut = xQueueCreate(1, sizeof(PetState));
  if (qEvents == NULL || qComfort == NULL || qStateOut == NULL) {
    Serial.println("FATAL: queue allocation failed");
    for (;;) { delay(1000); }
  }

  tHunger = xTimerCreate("hunger", pdMS_TO_TICKS(HUNGER_PERIOD_MS),
                         pdTRUE, NULL, hungerTimerCb);
  tStim   = xTimerCreate("stim",   pdMS_TO_TICKS(STIM_PERIOD_MS),
                         pdTRUE, NULL, stimTimerCb);
  if (tHunger == NULL || tStim == NULL) {
    Serial.println("FATAL: timer allocation failed");
    for (;;) { delay(1000); }
  }
  xTimerStart(tHunger, 0);
  xTimerStart(tStim, 0);

  attachInterrupt(digitalPinToInterrupt(PIN_IR_CALM), irIsr, FALLING);

  xTaskCreatePinnedToCore(InputTask,  "InputTask",  4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(GameTask,   "GameTask",   4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(ReportTask, "ReportTask", 8192, NULL, 1, NULL, 1);
}


void loop() {
  // RTOS handles scheduling
}