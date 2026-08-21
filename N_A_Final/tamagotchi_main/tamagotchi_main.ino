/* ============================================================================
 * CSE/EE 474 Final Project — Sensor-Driven Tamagotchi Pet (merged core logic)
 * Target: ESP32-S3, Arduino framework, FreeRTOS
 *
 * Merge of:
 *   EVERYTHING.ino        -- sensor acquisition (core 0)
 *   input_test_rtos.ino   -- user input + state machine (core 1)
 *
 * ---------------------------------------------------------------------------
 * TASK / CORE MAP
 *   Core 0  SensorTask  10 ms  prio 3  DHT11, HC-SR04, DS3231, PIR -> Queue #1
 *   Core 0  SoundTask   10 ms  prio 3  peak-to-peak sound amplitude
 *   Core 1  InputTask   10 ms  prio 3  button + IR + pot -> Queue #2 / qComfort
 *   Core 1  GameTask   100 ms  prio 2  SOLE OWNER of PetState
 *   Core 1  ReportTask   1 s   prio 1  serial state monitor (test harness)
 *
 * DATA FLOW
 *   Queue #1 (qSensor)  1-elem overwrite   SensorTask -> GameTask
 *   Queue #2 (qEvents)  depth 8            InputTask + timers -> GameTask
 *   qComfort            1-elem overwrite   InputTask -> GameTask
 *   qStateOut           1-elem overwrite    GameTask -> ReportTask (peek)
 *
 * TIMERS
 *   Timer #1 (tSlowSensor)  1 s auto-reload  paces DS3231 + DHT11 reads
 *   Timer #2 (tHunger)     30 s auto-reload  posts EVT_HUNGER_EXPIRE
 *
 * OWNERSHIP RULE
 *   Only GameTask writes petState. Sensor/input tasks and timer callbacks
 *   publish data or post events; ReportTask reads a published snapshot via
 *   xQueuePeek. No mutex is required on the state itself.
 *
 * SLEEP MODEL (two distinct states)
 *   SLEEPING     -- inactivity >= INACTIVITY_TIMEOUT_MS. CPU stays fully
 *                   awake; this is a behavioral state only. Exits on PIR
 *                   motion or a user input.
 *   HIBERNATING  -- RTC clock enters the night window. Tasks park at safe
 *                   points, timers stop, a DS3231 Alarm 1 is armed, and the
 *                   chip enters ESP32-S3 LIGHT SLEEP. Light sleep preserves
 *                   RAM/CPU/peripheral state, so execution resumes exactly
 *                   where it stopped. Wake is via ext0 on the DS3231 SQW pin
 *                   (open drain, active LOW).
 *
 *   NOTE: light sleep gates the ESP32's internal peripherals only. Sensors
 *   remain powered from the 3V3 rail by design decision, so hibernation
 *   reduces CPU/peripheral draw (tens of mA -> low single-digit mA), NOT
 *   sensor rail draw. Do not claim microamp figures in the writeup.
 *
 * WIRING ADDITIONS vs. the two source sketches
 *   DS3231 SQW -> GPIO 9  (RTC-capable; needs a pull-up, internal is enabled
 *                          here, an external 10k is more robust)
 *   HC-SR501   -> GPIO 10 (RTC-capable, 3.3V logic output)
 * ========================================================================== */

#include <Arduino.h>
#include <math.h>
#include <Wire.h>
#include <RTClib.h>
#include <DHT.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"


/* ============================== Pin map ================================== */
/* --- sensors (core 0) --- */
static const uint8_t PIN_SOUND    = 2;    // ADC1_CH1, analog sound module
static const uint8_t PIN_TRIG     = 5;    // HC-SR04 trigger
static const uint8_t PIN_ECHO     = 4;    // HC-SR04 echo
static const uint8_t PIN_SDA      = 6;    // DS3231
static const uint8_t PIN_SCL      = 7;    // DS3231
static const uint8_t PIN_DHT      = 8;    // DHT11 data
static const gpio_num_t PIN_RTC_SQW = GPIO_NUM_9;   // DS3231 alarm, active LOW
static const uint8_t PIN_PIR      = 10;   // HC-SR501 motion, active HIGH

/* --- user inputs (core 1) --- */
static const uint8_t PIN_BTN_FEED = 15;   // to GND, internal pull-up
static const uint8_t PIN_IR_CALM  = 16;   // KY-022 signal, idles HIGH
static const uint8_t PIN_POT      = 1;    // ADC1_CH0, comfort-band center

#define DHT_TYPE DHT11


/* ============================ Tunable config ============================= */
/* --- task periods --- */
static const TickType_t SENSOR_PERIOD = pdMS_TO_TICKS(10);    // 100 Hz target
static const TickType_t SOUND_PERIOD  = pdMS_TO_TICKS(10);    // 100 Hz target
static const TickType_t INPUT_PERIOD  = pdMS_TO_TICKS(10);    // 100 Hz target
static const TickType_t GAME_PERIOD   = pdMS_TO_TICKS(100);   //  10 Hz target
static const TickType_t REPORT_PERIOD = pdMS_TO_TICKS(1000);  //   1 Hz

/* --- software timers --- */
static const uint32_t SLOW_SENSOR_PERIOD_MS = 1000;   // Timer #1: RTC each tick
static const uint32_t HUNGER_PERIOD_MS      = 30000;  // Timer #2

/* --- comfort band (pot sets the center, DHT11 supplies the real temp) --- */
static const float BAND_WIDTH_C = 3.0f;
static const float BAND_HALF_C  = BAND_WIDTH_C / 2.0f;
static const float HYST_C       = 0.5f;    // edge hysteresis
static const float CENTER_MIN_C = 15.0f;   // pot fully CCW
static const float CENTER_MAX_C = 30.0f;   // pot fully CW

/* --- sound thresholds (same sensor, two jobs, so two levels) --- */
static const int SOUND_THRESHOLD          = 250;  // spike -> OVERSTIMULATED
static const int ACTIVITY_SOUND_THRESHOLD = 60;   // room noise -> "company"

/* --- inactivity / SLEEPING --- */
static const uint32_t INACTIVITY_TIMEOUT_MS = 300000UL;  // 5 min -> SLEEPING
static const uint32_t YAWN_LEAD_MS          = 30000UL;   // YAWNING before that

/* --- hibernation window (RTC hours, 24 h) --- */
#define HIBERNATION_ENABLED 1
static const int      NIGHT_START_HOUR    = 22;      // >= this hour is night
static const int      WAKE_HOUR           = 7;       // <  this hour is night
static const uint32_t HIBERNATE_GRACE_MS  = 15000UL; // no sleep right at boot
static const uint32_t POST_WAKE_BLOCK_MS  = 60000UL; // no immediate re-entry

/* --- input conditioning --- */
static const uint32_t BTN_DEBOUNCE_MS = 50;   // stable-window debounce
static const uint32_t IR_LOCKOUT_MS   = 200;  // rejects NEC repeat frames

/* --- sensor pacing --- */
static const uint8_t  DHT_EVERY_N_SLOW_TICKS = 2;      // DHT11 needs >= 1 s
static const uint8_t  SONAR_EVERY_N_CYCLES   = 5;      // 20 Hz, pulseIn blocks
static const uint32_t SONAR_TIMEOUT_US       = 30000;  // worst-case block
static const uint32_t SOUND_SAMPLE_US        = 2000;   // busy-wait window

/* --- debug output --- */
#define VERBOSE_TRANSITIONS 1   // set 0 for clean timing measurements
static const uint32_t RATE_WINDOW_MS = 5000;


/* ================================ Types ================================= */
typedef enum {
  ST_HAPPY = 0,
  ST_HUNGRY,
  ST_OVERSTIMULATED,
  ST_HOT,
  ST_COLD,
  ST_YAWNING,
  ST_SLEEPING,
  ST_HIBERNATING
} PetState;

static const char *STATE_NAME[] = {
  "HAPPY", "HUNGRY", "OVERSTIMULATED", "HOT", "COLD",
  "YAWNING", "SLEEPING", "HIBERNATING"
};

typedef struct {
  float temp;         // C, DHT11
  float humidity;     // %RH, DHT11
  int   soundLevel;   // raw ADC peak-to-peak
  float distance;     // cm, -1 on timeout
  bool  motion;       // PIR level
  int   hour;
  int   minute;
  int   second;
} SensorData;

typedef enum {
  EVT_FEED = 0,          // debounced button
  EVT_CALM,              // IR remote
  EVT_HUNGER_EXPIRE      // Timer #2
} EventType;

typedef struct {
  EventType type;
  uint32_t  t_ms;
} InputEvent;

/* Snapshot published for ReportTask / future LCD task. */
typedef struct {
  PetState state;
  float    temp;
  float    humidity;
  float    distance;
  float    center;
  float    lo;
  float    hi;
  int      soundLevel;
  bool     motion;
  int      hour;
  int      minute;
  int      second;
  bool     hungry;
  bool     overstim;
  bool     hot;
  bool     cold;
  uint32_t msSinceActivity;
} StateSnapshot;


/* ============================== Globals ================================= */
RTC_DS3231 rtc;
DHT        dht(PIN_DHT, DHT_TYPE);

static QueueHandle_t qSensor   = NULL;   // Queue #1
static QueueHandle_t qEvents   = NULL;   // Queue #2
static QueueHandle_t qComfort  = NULL;
static QueueHandle_t qStateOut = NULL;

static TimerHandle_t tSlowSensor = NULL; // Timer #1
static TimerHandle_t tHunger     = NULL; // Timer #2

/* ADC1 is shared by PIN_SOUND (core 0) and PIN_POT (core 1). Arduino's
   analogRead() is not safe for concurrent use across cores on one ADC unit,
   so every analogRead() below is taken under this mutex. */
static SemaphoreHandle_t adcMutex = NULL;

static volatile uint32_t g_irEdges = 0;  // bumped by ISR only

static volatile bool g_rtcDue = false;   // set by Timer #1
static volatile bool g_dhtDue = false;   // set by Timer #1

/* Latest sound amplitude, produced by SoundTask, consumed by SensorTask. */
static volatile int g_soundLevel = 0;

/* Cooperative park handshake used before entering light sleep. GameTask sets
   g_parkReq; each other task acknowledges at the top of its loop, waits, then
   re-initializes its own lastWake on release (the FreeRTOS tick is suspended
   during light sleep, so every stored wake time is stale afterwards). */
static volatile bool g_parkReq  = false;
static volatile bool g_parkedSensor = false;
static volatile bool g_parkedSound  = false;
static volatile bool g_parkedInput  = false;
static volatile bool g_parkedReport = false;

/* Cycle counters for measured-frequency verification. */
static volatile uint32_t g_cycSensor = 0;
static volatile uint32_t g_cycSound  = 0;
static volatile uint32_t g_cycInput  = 0;
static volatile uint32_t g_cycGame   = 0;


/* ================================ ISR =================================== */
static void IRAM_ATTR irIsr() {
  g_irEdges++;
}


/* =========================== Timer callbacks ============================ */
/* Timer #1 -- paces the two slow sensors so SensorTask no longer carries
   magic loop counters. RTC every tick, DHT11 every Nth tick. */
static void slowSensorTimerCb(TimerHandle_t) {
  static uint8_t n = 0;
  g_rtcDue = true;
  if (++n >= DHT_EVERY_N_SLOW_TICKS) {
    n = 0;
    g_dhtDue = true;
  }
}

/* Timer #2 -- hunger. Posts an event; never touches state directly. */
static void hungerTimerCb(TimerHandle_t) {
  InputEvent e = { EVT_HUNGER_EXPIRE, (uint32_t)millis() };
  xQueueSend(qEvents, &e, 0);
}


/* ============================== Helpers ================================= */
static int readAdcLocked(uint8_t pin) {
  int v = 0;
  if (xSemaphoreTake(adcMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    v = analogRead(pin);
    xSemaphoreGive(adcMutex);
  }
  return v;
}

/* HC-SR04. pulseIn() is blocking: worst case SONAR_TIMEOUT_US (30 ms), which
   exceeds the 10 ms SensorTask period. Called only every SONAR_EVERY_N_CYCLES
   to limit the damage; the measured-rate output will expose the real cost. */
static float readDistance() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long duration = pulseIn(PIN_ECHO, HIGH, SONAR_TIMEOUT_US);
  if (duration == 0) {
    return -1.0f;
  }
  return duration / 58.0f;
}

static bool isNightHour(int hour) {
  return (hour >= NIGHT_START_HOUR) || (hour < WAKE_HOUR);
}

/* Park handshake, called at the top of every non-GameTask loop. Returns true
   if a park actually happened, meaning the caller must reset its lastWake. */
static bool parkIfRequested(volatile bool *ackFlag) {
  if (!g_parkReq) {
    return false;
  }
  *ackFlag = true;
  while (g_parkReq) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  *ackFlag = false;
  return true;
}


/* ============================== SoundTask =============================== */
/* Core 0. Peak-to-peak amplitude over a short busy-wait window. The window is
   deliberately short (2 ms of every 10 ms) so it does not starve SensorTask,
   which shares this core at the same priority. */
static void SoundTask(void *) {
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    if (parkIfRequested(&g_parkedSound)) {
      lastWake = xTaskGetTickCount();
    }

    int minValue = 4095;
    int maxValue = 0;

    if (xSemaphoreTake(adcMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
      unsigned long start = micros();
      while (micros() - start < SOUND_SAMPLE_US) {
        int sample = analogRead(PIN_SOUND);
        if (sample < minValue) minValue = sample;
        if (sample > maxValue) maxValue = sample;
      }
      xSemaphoreGive(adcMutex);
      g_soundLevel = maxValue - minValue;
    }

    g_cycSound++;
    vTaskDelayUntil(&lastWake, SOUND_PERIOD);
  }
}


/* ============================== SensorTask ============================== */
/* Core 0. Owns DHT11, HC-SR04, DS3231 and the PIR. Publishes the latest
   reading into the 1-element overwrite Queue #1 every cycle, so GameTask
   always sees fresh data instead of a stale backlog. */
static void SensorTask(void *) {
  TickType_t lastWake = xTaskGetTickCount();

  SensorData data;
  data.temp       = 22.0f;   // seeded so the first GameTask cycles are sane
  data.humidity   = 50.0f;
  data.soundLevel = 0;
  data.distance   = -1.0f;
  data.motion     = false;
  data.hour       = 12;
  data.minute     = 0;
  data.second     = 0;

  uint8_t sonarCounter = 0;

  for (;;) {
    if (parkIfRequested(&g_parkedSensor)) {
      lastWake = xTaskGetTickCount();
    }

    /* DHT11 -- paced by Timer #1. Keep the previous value on a failed read
       so a single NaN cannot corrupt the comfort-band logic. */
    if (g_dhtDue) {
      g_dhtDue = false;
      float t = dht.readTemperature();
      float h = dht.readHumidity();
      if (!isnan(t)) data.temp = t;
      if (!isnan(h)) data.humidity = h;
    }

    /* HC-SR04 -- rate limited because pulseIn() blocks. */
    if (++sonarCounter >= SONAR_EVERY_N_CYCLES) {
      sonarCounter = 0;
      data.distance = readDistance();
    }

    /* Sound amplitude from SoundTask. */
    data.soundLevel = g_soundLevel;

    /* PIR. HC-SR501 holds its output HIGH for its retrigger window (seconds),
       so sampling the level here is sufficient -- no edge latch needed. */
    data.motion = (digitalRead(PIN_PIR) == HIGH);

    /* DS3231 -- paced by Timer #1. */
    if (g_rtcDue) {
      g_rtcDue = false;
      DateTime now = rtc.now();
      data.hour   = now.hour();
      data.minute = now.minute();
      data.second = now.second();
    }

    xQueueOverwrite(qSensor, &data);

    g_cycSensor++;
    vTaskDelayUntil(&lastWake, SENSOR_PERIOD);
  }
}


/* =============================== InputTask ============================== */
/* Core 1. Stable-window debounce on the button, lockout debounce on the IR
   receiver, EMA smoothing on the pot. Posts discrete events to Queue #2 and
   the smoothed comfort center to qComfort. */
static void InputTask(void *) {
  TickType_t lastWake = xTaskGetTickCount();

  int      btnRaw        = HIGH;
  int      btnStable     = HIGH;
  uint32_t btnLastChange = 0;

  uint32_t irSeen       = 0;
  uint32_t irLastAccept = 0;

  float potEma = (float)readAdcLocked(PIN_POT);

  for (;;) {
    if (parkIfRequested(&g_parkedInput)) {
      lastWake = xTaskGetTickCount();
    }

    const uint32_t now = millis();

    /* Button: accept a new level only after it has been stable for
       BTN_DEBOUNCE_MS, then act on the falling (pressed) edge. */
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

    /* IR: the ISR only counts edges. One NEC frame produces many edges, so
       accept at most one CALM per IR_LOCKOUT_MS. */
    const uint32_t edges = g_irEdges;
    if (edges != irSeen) {
      irSeen = edges;
      if ((now - irLastAccept) >= IR_LOCKOUT_MS) {
        irLastAccept = now;
        InputEvent e = { EVT_CALM, now };
        xQueueSend(qEvents, &e, 0);
      }
    }

    /* Pot -> comfort band center, EMA smoothed against ADC noise. */
    potEma = (potEma * 0.9f) + ((float)readAdcLocked(PIN_POT) * 0.1f);
    float center = CENTER_MIN_C +
                   (potEma / 4095.0f) * (CENTER_MAX_C - CENTER_MIN_C);
    xQueueOverwrite(qComfort, &center);

    g_cycInput++;
    vTaskDelayUntil(&lastWake, INPUT_PERIOD);
  }
}


/* ============================== Hibernation ============================= */
/* Arms DS3231 Alarm 1 for WAKE_HOUR:00:00 and enters ESP32-S3 light sleep.
   Called from GameTask only. Returns after the chip has woken. */
static void enterHibernation() {
  Serial.println("[HIB] entering hibernation");
  Serial.flush();

  /* 1. Stop the software timers so they cannot fire a backlog on wake. */
  xTimerStop(tSlowSensor, 0);
  xTimerStop(tHunger, 0);

  /* 2. Park the other tasks at safe points -- never mid-I2C or mid-pulseIn.
        Bounded wait so a stuck task cannot deadlock the pet. */
  g_parkReq = true;
  const uint32_t parkDeadline = millis() + 1500;   // ReportTask runs at 1 Hz
  while (millis() < parkDeadline) {
    if (g_parkedSensor && g_parkedSound && g_parkedInput && g_parkedReport) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }

  /* 3. Arm the RTC alarm. SQW must leave square-wave mode or setAlarm1 fails. */
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  rtc.writeSqwPinMode(DS3231_OFF);
  rtc.disableAlarm(2);

  DateTime now  = rtc.now();
  DateTime wake(now.year(), now.month(), now.day(), WAKE_HOUR, 0, 0);
  if (wake <= now) {
    wake = wake + TimeSpan(1, 0, 0, 0);   // WAKE_HOUR already passed today
  }
  if (!rtc.setAlarm1(wake, DS3231_A1_Hour)) {
    Serial.println("[HIB] alarm arm FAILED, staying awake");
    xTimerStart(tSlowSensor, 0);
    xTimerStart(tHunger, 0);
    g_parkReq = false;
    return;
  }

  Serial.printf("[HIB] now %02d:%02d, alarm %02d:00, light sleep now\n",
                now.hour(), now.minute(), wake.hour());
  Serial.flush();

  /* 4. ext0 wake on SQW. Open drain, active LOW, so wake on level 0. */
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  rtc_gpio_pullup_en(PIN_RTC_SQW);
  rtc_gpio_pulldown_dis(PIN_RTC_SQW);
  esp_sleep_enable_ext0_wakeup(PIN_RTC_SQW, 0);

  /* 5. Sleep. RAM, CPU context and peripheral state are preserved, so
        execution continues on the next line after wake. */
  esp_light_sleep_start();

  /* ---- awake ---- */
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  rtc.clearAlarm(1);
  rtc.disableAlarm(1);

  xTimerStart(tSlowSensor, 0);
  xTimerStart(tHunger, 0);

  /* 6. Release the parked tasks; each resets its own lastWake. */
  g_parkReq = false;

  Serial.printf("[HIB] woke, cause=%d\n", (int)cause);
}


/* =============================== GameTask =============================== */
/* Core 1. Sole owner of petState. Drains Queue #2, reads the latest sensor
   snapshot from Queue #1, then recomputes a single PetState by priority:
   HIBERNATING > SLEEPING > YAWNING > OVERSTIMULATED > HOT/COLD > HUNGRY >
   HAPPY. Latching conditions (hunger, overstimulation) are held in local
   flags: they are inputs to the state computation, not a second state. */
static void GameTask(void *) {
  TickType_t lastWake = xTaskGetTickCount();

  PetState state     = ST_HAPPY;
  PetState prevState = ST_HAPPY;

  bool hungryLatched   = false;
  bool overstimLatched = false;
  bool hot = false, cold = false;

  float      center = (CENTER_MIN_C + CENTER_MAX_C) / 2.0f;
  SensorData sd;
  bool       haveSensor = false;

  uint32_t lastActivityMs   = millis();
  uint32_t hibBlockUntilMs  = HIBERNATE_GRACE_MS;

  /* Seed so the first cycles before any sensor publish are still valid. */
  sd.temp = 22.0f; sd.humidity = 50.0f; sd.soundLevel = 0;
  sd.distance = -1.0f; sd.motion = false;
  sd.hour = 12; sd.minute = 0; sd.second = 0;

  for (;;) {
    const uint32_t now = millis();
    bool activity = false;

    /* ---- Queue #2: discrete events ---- */
    InputEvent e;
    while (xQueueReceive(qEvents, &e, 0) == pdTRUE) {
      switch (e.type) {
        case EVT_FEED:
          hungryLatched = false;
          xTimerReset(tHunger, 0);
          activity = true;
#if VERBOSE_TRANSITIONS
          Serial.println("[EVT] FEED -> hunger cleared, Timer #2 reset");
#endif
          break;

        case EVT_CALM:
          overstimLatched = false;
          activity = true;
#if VERBOSE_TRANSITIONS
          Serial.println("[EVT] CALM -> overstimulation cleared");
#endif
          break;

        case EVT_HUNGER_EXPIRE:
          hungryLatched = true;
          break;
      }
    }

    /* ---- pot comfort center ---- */
    xQueueReceive(qComfort, &center, 0);

    /* ---- Queue #1: latest sensor snapshot ---- */
    if (xQueueReceive(qSensor, &sd, 0) == pdTRUE) {
      haveSensor = true;
    }

    /* ---- activity detection ----
       Motion and user inputs always count as presence. Sound counts only
       while the pet is awake: the spec is that SLEEPING is exited by motion,
       so ambient noise must not by itself end a nap. */
    if (sd.motion) {
      activity = true;
    }
    if (state != ST_SLEEPING && sd.soundLevel >= ACTIVITY_SOUND_THRESHOLD) {
      activity = true;
    }
    if (activity) {
      lastActivityMs = now;
    }
    const uint32_t idleMs = now - lastActivityMs;

    /* ---- overstimulation latch (suppressed while asleep) ---- */
    if (state != ST_SLEEPING && sd.soundLevel > SOUND_THRESHOLD) {
      overstimLatched = true;
    }

    /* ---- comfort band with hysteresis, driven by the real DHT11 temp ---- */
    const float hi = center + BAND_HALF_C;
    const float lo = center - BAND_HALF_C;

    hot  = hot  ? (sd.temp > (hi - HYST_C)) : (sd.temp > hi);
    cold = cold ? (sd.temp < (lo + HYST_C)) : (sd.temp < lo);
    if (hot && cold) {
      cold = false;   // cannot be both; hot wins
    }

    /* ---- state computation, single enum, strict priority ---- */
#if HIBERNATION_ENABLED
    if (haveSensor && isNightHour(sd.hour) && now >= hibBlockUntilMs) {
      state = ST_HIBERNATING;
      enterHibernation();
      /* Fresh start after wake: tick count moved, so re-seed everything. */
      lastWake         = xTaskGetTickCount();
      lastActivityMs   = millis();
      hibBlockUntilMs  = millis() + POST_WAKE_BLOCK_MS;
      state            = ST_HAPPY;
      prevState        = ST_HIBERNATING;
      g_cycGame++;
      vTaskDelayUntil(&lastWake, GAME_PERIOD);
      continue;
    }
#endif

    if (idleMs >= INACTIVITY_TIMEOUT_MS) {
      state = ST_SLEEPING;
    } else if (idleMs >= (INACTIVITY_TIMEOUT_MS - YAWN_LEAD_MS)) {
      state = ST_YAWNING;
    } else if (overstimLatched) {
      state = ST_OVERSTIMULATED;
    } else if (hot) {
      state = ST_HOT;
    } else if (cold) {
      state = ST_COLD;
    } else if (hungryLatched) {
      state = ST_HUNGRY;
    } else {
      state = ST_HAPPY;
    }

#if VERBOSE_TRANSITIONS
    if (state != prevState) {
      Serial.printf("[STATE] %s -> %s\n",
                    STATE_NAME[prevState], STATE_NAME[state]);
    }
#endif
    prevState = state;

    /* ---- publish snapshot for ReportTask / future LCD task ---- */
    StateSnapshot snap;
    snap.state           = state;
    snap.temp            = sd.temp;
    snap.humidity        = sd.humidity;
    snap.distance        = sd.distance;
    snap.center          = center;
    snap.lo              = lo;
    snap.hi              = hi;
    snap.soundLevel      = sd.soundLevel;
    snap.motion          = sd.motion;
    snap.hour            = sd.hour;
    snap.minute          = sd.minute;
    snap.second          = sd.second;
    snap.hungry          = hungryLatched;
    snap.overstim        = overstimLatched;
    snap.hot             = hot;
    snap.cold            = cold;
    snap.msSinceActivity = idleMs;
    xQueueOverwrite(qStateOut, &snap);

    g_cycGame++;
    vTaskDelayUntil(&lastWake, GAME_PERIOD);
  }
}


/* ============================== ReportTask ============================== */
/* Core 1, lowest priority. Test-harness state monitor. Reads a GameTask
   snapshot with xQueuePeek so a valid copy always remains available, and
   never touches petState directly -- no mutex needed. Also reports MEASURED
   task frequencies, which is the only honest way to check the 100/10 Hz
   targets. Set VERBOSE_TRANSITIONS to 0 while taking timing measurements. */
static void ReportTask(void *) {
  TickType_t lastWake = xTaskGetTickCount();
  StateSnapshot snap;

  uint32_t lastRateMs = millis();

  for (;;) {
    if (parkIfRequested(&g_parkedReport)) {
      lastWake = xTaskGetTickCount();
    }

    if (xQueuePeek(qStateOut, &snap, 0) == pdTRUE) {
      Serial.printf(
        "[STATE] %-14s | %02d:%02d:%02d | %.1fC %.0f%%RH | band %.1f-%.1f "
        "(center %.1f) | snd %4d | dist %6.1fcm | PIR %d | "
        "HOT%d COLD%d OVER%d HUNGRY%d | idle %lus\n",
        STATE_NAME[snap.state],
        snap.hour, snap.minute, snap.second,
        snap.temp, snap.humidity,
        snap.lo, snap.hi, snap.center,
        snap.soundLevel, snap.distance, snap.motion ? 1 : 0,
        snap.hot ? 1 : 0, snap.cold ? 1 : 0,
        snap.overstim ? 1 : 0, snap.hungry ? 1 : 0,
        (unsigned long)(snap.msSinceActivity / 1000));
    }

    const uint32_t now = millis();
    if ((now - lastRateMs) >= RATE_WINDOW_MS) {
      const uint32_t sc = g_cycSensor, so = g_cycSound;
      const uint32_t ic = g_cycInput,  gc = g_cycGame;
      g_cycSensor = 0; g_cycSound = 0; g_cycInput = 0; g_cycGame = 0;
      const float win = (float)(now - lastRateMs);
      Serial.printf(
        "[RATE] Sensor %.1f Hz | Sound %.1f Hz | Input %.1f Hz | Game %.1f Hz\n",
        sc * 1000.0f / win, so * 1000.0f / win,
        ic * 1000.0f / win, gc * 1000.0f / win);
      lastRateMs = now;
    }

    vTaskDelayUntil(&lastWake, REPORT_PERIOD);
  }
}


/* ================================ setup ================================= */
void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Tamagotchi Pet: merged sensor + input core ===");

  /* --- pin modes --- */
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BTN_FEED, INPUT_PULLUP);
  pinMode(PIN_IR_CALM, INPUT);
  pinMode(PIN_RTC_SQW, INPUT_PULLUP);   // DS3231 SQW is open drain
  analogReadResolution(12);
  /* DHT11 and the analog sound module need no manual pinMode. */

  /* --- I2C + RTC --- */
  Wire.begin(PIN_SDA, PIN_SCL);
  if (!rtc.begin()) {
    Serial.println("FATAL: DS3231 not found");
    for (;;) { delay(1000); }
  }
  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting time from build timestamp");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  /* Leave SQW as a plain interrupt line and clear anything stale. */
  rtc.writeSqwPinMode(DS3231_OFF);
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  rtc.disableAlarm(1);
  rtc.disableAlarm(2);

  dht.begin();

  /* --- ADC guard --- */
  adcMutex = xSemaphoreCreateMutex();
  if (adcMutex == NULL) {
    Serial.println("FATAL: ADC mutex allocation failed");
    for (;;) { delay(1000); }
  }

  /* --- queues --- */
  qSensor   = xQueueCreate(1, sizeof(SensorData));      // Queue #1, overwrite
  qEvents   = xQueueCreate(8, sizeof(InputEvent));      // Queue #2
  qComfort  = xQueueCreate(1, sizeof(float));
  qStateOut = xQueueCreate(1, sizeof(StateSnapshot));
  if (qSensor == NULL || qEvents == NULL ||
      qComfort == NULL || qStateOut == NULL) {
    Serial.println("FATAL: queue allocation failed");
    for (;;) { delay(1000); }
  }

  /* --- timers --- */
  tSlowSensor = xTimerCreate("slowSensor",
                             pdMS_TO_TICKS(SLOW_SENSOR_PERIOD_MS),
                             pdTRUE, NULL, slowSensorTimerCb);
  tHunger     = xTimerCreate("hunger",
                             pdMS_TO_TICKS(HUNGER_PERIOD_MS),
                             pdTRUE, NULL, hungerTimerCb);
  if (tSlowSensor == NULL || tHunger == NULL) {
    Serial.println("FATAL: timer allocation failed");
    for (;;) { delay(1000); }
  }
  xTimerStart(tSlowSensor, 0);
  xTimerStart(tHunger, 0);

  /* Force a first read of both slow sensors immediately. */
  g_rtcDue = true;
  g_dhtDue = true;

  /* --- IR edge counter --- */
  attachInterrupt(digitalPinToInterrupt(PIN_IR_CALM), irIsr, FALLING);

  /* --- tasks --- */
  xTaskCreatePinnedToCore(SensorTask, "SensorTask", 4096, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(SoundTask,  "SoundTask",  4096, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(InputTask,  "InputTask",  4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(GameTask,   "GameTask",   4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(ReportTask, "ReportTask", 8192, NULL, 1, NULL, 1);

  Serial.println("setup complete, RTOS running");
}


void loop() {
  /* Everything runs in tasks. */
}
