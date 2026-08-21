/* ============================================================================
 * CSE/EE 474 Final Project — Sensor-Driven Tamagotchi Pet (FINAL)
 * Target: ESP32-S3, Arduino framework, FreeRTOS
 *
 * Supersedes tamagotchi_main.ino. Adds the LCD feature from EVERYTHING.ino as
 * a dedicated display task on a dedicated I2C bus.
 *
 * ---------------------------------------------------------------------------
 * HARDWARE CHANGES vs. tamagotchi_main.ino
 *
 *   LCD ONLY. Nothing else on the breadboard moves.
 *
 *   ADD  16x2 HD44780 + PCF8574 I2C backpack:
 *          SDA -> GPIO 17   (new, Wire1)
 *          SCL -> GPIO 18   (new, Wire1)
 *          VCC -> 5V, GND -> GND
 *          See "LCD ELECTRICAL" below for the pull-up caveat.
 *
 *   UNCHANGED: PIN_SOUND 2, PIN_POT 1, PIN_TRIG 5, PIN_ECHO 4, PIN_SDA 6,
 *   PIN_SCL 7, PIN_DHT 8, PIN_RTC_SQW 9, PIN_PIR 10, PIN_BTN_FEED 15,
 *   PIN_IR_CALM 16. No pin was reassigned, repurposed, or freed. Every
 *   pinMode(), the DS3231 bus, the ADC mutex and the ext0 wake source are
 *   byte-for-byte the same as before. If the LCD is unplugged this sketch
 *   still runs the full pet on serial alone.
 *
 * LCD ELECTRICAL
 *   GPIO 17/18 are in the ESP32-S3 general-purpose pool: clear of the
 *   strapping pins (0/3/45/46), the native USB pins (19/20), the flash+PSRAM
 *   bus (26-32) and the module-dependent octal-PSRAM group (33-37). They are
 *   ADC2 channels, which is irrelevant here -- both analog inputs (sound on 2,
 *   pot on 1) are ADC1 and keep using adcMutex.
 *
 *   The backpack wants 5V for readable contrast, but its onboard pull-ups then
 *   pull SDA/SCL to 5V and the ESP32-S3 is NOT 5V tolerant. Pick one:
 *     (a) power at 5V, remove/lift the backpack's two pull-up resistors, fit
 *         4.7k pull-ups to 3V3 instead                    <- recommended
 *     (b) power at 5V through a 2-channel I2C level shifter
 *     (c) power the whole module at 3.3V and turn the contrast trimpot up
 *   Turn the contrast trimpot before assuming the code is broken. A blank or
 *   solid-black screen is almost always contrast, not firmware.
 *
 * LIBRARY CHANGE
 *   Uses LiquidCrystal_PCF8574 (Matthias Hertel), NOT LiquidCrystal_I2C.
 *   LiquidCrystal_I2C talks to the global Wire object internally and cannot be
 *   pointed at Wire1, which is the whole point of the second bus. The PCF8574
 *   library accepts the port in begin(): begin(cols, rows, TwoWire&).
 *   Install via Library Manager: "LiquidCrystal_PCF8574".
 *
 * ---------------------------------------------------------------------------
 * TASK / CORE MAP
 *   Core 0  SensorTask  10 ms  prio 3  DHT11, HC-SR04, DS3231, PIR -> Queue #1
 *   Core 0  SoundTask   10 ms  prio 3  peak-to-peak sound amplitude
 *   Core 1  InputTask   10 ms  prio 3  button + IR + pot -> Queue #2 / qComfort
 *   Core 1  GameTask   100 ms  prio 2  SOLE OWNER of PetState + ScreenId
 *   Core 1  LcdTask    200 ms  prio 1  renders the snapshot to the 16x2   NEW
 *   Core 1  ReportTask   1 s   prio 1  serial state monitor (test harness)
 *
 *   LcdTask sits at the lowest priority on purpose. A 16x2 over a PCF8574 is
 *   slow: each character is two nibbles and each nibble costs three expander
 *   byte-writes, so a full 32-character repaint at 100 kHz is roughly 20-40 ms.
 *   Running that inside GameTask (as EVERYTHING.ino did) would eat a third of
 *   the 100 ms budget. Here it cannot delay game logic or debouncing, and the
 *   shadow-buffer diff below means the steady state costs zero I2C traffic.
 *
 * DATA FLOW
 *   Queue #1 (qSensor)  1-elem overwrite   SensorTask -> GameTask
 *   Queue #2 (qEvents)  depth 8            InputTask + timers -> GameTask
 *   qComfort            1-elem overwrite   InputTask -> GameTask
 *   qStateOut           1-elem overwrite   GameTask -> ReportTask + LcdTask
 *                                          (both read with xQueuePeek)
 *
 * TIMERS
 *   Timer #1 (tSlowSensor)  1 s auto-reload  paces DS3231 + DHT11 reads
 *   Timer #2 (tHunger)     30 s auto-reload  posts EVT_HUNGER_EXPIRE
 *
 * I2C TOPOLOGY
 *   Wire  (GPIO 6/7,  400 kHz)  DS3231 @ 0x68        driven from core 0
 *   Wire1 (GPIO 17/18, 100 kHz) PCF8574 @ 0x27/0x3F  driven from core 1
 *   Two independent controllers, so no bus is ever touched from both cores and
 *   no I2C mutex is required. The rejected alternative was one bus plus a
 *   mutex around every rtc.now() and every LCD write, which couples the cores.
 *
 * OWNERSHIP RULE
 *   Only GameTask writes petState and screen. Sensor/input tasks and timer
 *   callbacks publish data or post events. ReportTask and LcdTask read a
 *   published snapshot via xQueuePeek. No mutex on the state itself.
 *
 *   Single documented exception: enterHibernation() draws the goodnight screen
 *   directly. That is safe because the park handshake has already stopped
 *   LcdTask, so the park IS the mutual exclusion.
 *
 * SLEEP MODEL (two distinct states)
 *   SLEEPING     -- inactivity >= INACTIVITY_TIMEOUT_MS. CPU stays fully
 *                   awake; behavioral state only. Exits on PIR motion, an
 *                   HC-SR04 presence detection, or a user input.
 *   HIBERNATING  -- RTC clock enters the night window. Tasks park at safe
 *                   points, timers stop, the LCD backlight goes off, a DS3231
 *                   Alarm 1 is armed, and the chip enters ESP32-S3 LIGHT
 *                   SLEEP. Light sleep preserves RAM/CPU/peripheral state, so
 *                   execution resumes exactly where it stopped. Wake is via
 *                   ext0 on the DS3231 SQW pin (open drain, active LOW).
 *
 *   NOTE: light sleep gates the ESP32's internal peripherals only. Sensors and
 *   the LCD module remain powered from their rails by design decision, so
 *   hibernation reduces CPU/peripheral draw (tens of mA -> low single-digit
 *   mA) plus the ~20 mA of LCD backlight, NOT sensor rail draw. Do not claim
 *   microamp figures in the writeup.
 * ========================================================================== */

#include <Arduino.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <Wire.h>
#include <RTClib.h>
#include <DHT.h>
#include <LiquidCrystal_PCF8574.h>
#include "esp_sleep.h"
#include "driver/rtc_io.h"


/* ============================== Pin map ================================== */
/* --- sensors (core 0) --- UNCHANGED from tamagotchi_main.ino --- */
static const uint8_t PIN_SOUND    = 2;    // ADC1_CH1, analog sound module
static const uint8_t PIN_TRIG     = 5;    // HC-SR04 trigger
static const uint8_t PIN_ECHO     = 4;    // HC-SR04 echo
static const uint8_t PIN_SDA      = 6;    // DS3231
static const uint8_t PIN_SCL      = 7;    // DS3231
static const uint8_t PIN_DHT      = 8;    // DHT11 data
static const gpio_num_t PIN_RTC_SQW = GPIO_NUM_9;   // DS3231 alarm, active LOW
static const uint8_t PIN_PIR      = 10;   // HC-SR501 motion, active HIGH

/* --- user inputs (core 1) --- UNCHANGED from tamagotchi_main.ino --- */
static const uint8_t PIN_BTN_FEED = 15;   // to GND, internal pull-up
static const uint8_t PIN_IR_CALM  = 16;   // KY-022 signal, idles HIGH
static const uint8_t PIN_POT      = 1;    // ADC1_CH0, comfort-band center

/* --- display (core 1) --- THE ONLY NEW HARDWARE --- */
static const uint8_t PIN_LCD_SDA  = 17;   // Wire1, PCF8574 backpack
static const uint8_t PIN_LCD_SCL  = 18;   // Wire1, PCF8574 backpack

#define DHT_TYPE DHT11


/* ============================ Tunable config ============================= */
/* --- task periods --- */
static const TickType_t SENSOR_PERIOD = pdMS_TO_TICKS(10);    // 100 Hz target
static const TickType_t SOUND_PERIOD  = pdMS_TO_TICKS(10);    // 100 Hz target
static const TickType_t INPUT_PERIOD  = pdMS_TO_TICKS(10);    // 100 Hz target
static const TickType_t GAME_PERIOD   = pdMS_TO_TICKS(100);   //  10 Hz target
static const TickType_t LCD_PERIOD    = pdMS_TO_TICKS(200);   //   5 Hz
static const TickType_t REPORT_PERIOD = pdMS_TO_TICKS(1000);  //   1 Hz

/* --- software timers --- */
static const uint32_t SLOW_SENSOR_PERIOD_MS = 1000;   // Timer #1: RTC each tick
static const uint32_t HUNGER_PERIOD_MS      = 120000;  // Timer #2

/* --- comfort band (pot sets the center, DHT11 supplies the real temp) --- */
static const float BAND_WIDTH_C = 3.0f;
static const float BAND_HALF_C  = BAND_WIDTH_C / 2.0f;
static const float HYST_C       = 0.5f;    // edge hysteresis
static const float CENTER_MIN_C = 15.0f;   // pot fully CCW
static const float CENTER_MAX_C = 30.0f;   // pot fully CW

/* --- sound thresholds (same sensor, two jobs, so two levels) --- */
static const int SOUND_THRESHOLD          = 350;  // spike -> OVERSTIMULATED
static const int ACTIVITY_SOUND_THRESHOLD = 250;   // room noise -> "company"

/* --- HC-SR04 presence, hysteresis so the edge cannot chatter --- */
/* --- HC-SR04 presence, hysteresis so the edge cannot chatter ---
   (still used for the LCD's SCREEN_ATTRACT/SCREEN_SPLASH transition
   only -- no longer consulted for sleep/wake, see DISTANCE_MOTION_*
   below) */
static const float    USER_ENTER_CM   = 50.0f;  // <= this -> present
static const float    USER_EXIT_CM    = 60.0f;  // >= this -> absent
static const uint8_t  PRESENCE_MISSES = 3;      // consecutive far/timeouts
                                                // needed to drop presence

/* --- sonar-based MOTION detection, decoupled from presence ---
   A delta between successive valid sd.distance readings larger than
   DISTANCE_MOTION_TOLERANCE_CM counts as motion for exactly one
   GameTask cycle. This is intentionally simpler than the old v4
   confirm-before-commit logic (no persistence window) because motion
   detection should be immediate, not delayed -- delay is only useful
   when you are trying to IGNORE noise for a binary presence decision,
   not when you are trying to detect a wake-worthy event. If sonar
   noise alone is large enough to spuriously trigger this, raise the
   tolerance or apply the DISTANCE_EMA_ALPHA smoothing patch first --
   smoothing the input makes a fixed tolerance far more reliable here. */
static const float DISTANCE_MOTION_TOLERANCE_CM = 3.0f;  // > this = motion

/* --- display --- */
static const uint32_t SPLASH_MS       = 2000;   // "Welcome Back!" hold
#define RETURN_TO_ATTRACT_ON_EXIT 1             // 0 = stay on the state screen
static const uint8_t  LCD_COLS        = 16;
static const uint8_t  LCD_ROWS        = 2;
static const uint8_t  LCD_ADDR_PRIMARY = 0x27;  // most backpacks
static const uint8_t  LCD_ADDR_ALT     = 0x3F;  // the other common one
static const uint8_t  LCD_BACKLIGHT_ON = 255;

/* --- inactivity / SLEEPING --- */
static const uint32_t INACTIVITY_TIMEOUT_MS = 30000UL;  // 5 min -> SLEEPING
static const uint32_t YAWN_LEAD_MS          = 15000UL;   // YAWNING before that

/* --- hibernation window (RTC hour + minute, minute resolution) --- */
#define HIBERNATION_ENABLED 1
static const int      NIGHT_START_HOUR    = 10;      // hibernation start hour
static const int      NIGHT_START_MINUTE  = 40;        // hibernation start minute
static const int      WAKE_HOUR           = 10;       // wake hour
static const int      WAKE_MINUTE         = 42;        // wake minute
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
static const float    DISTANCE_EMA_ALPHA     = 0.3f;   // sonar smoothing weight

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

/* Short forms so row 0 fits "Pet: " + name inside 16 columns. Indexed by
   PetState, so this array and STATE_NAME must stay the same length. */
static const char *STATE_SHORT[] = {
  "HAPPY", "HUNGRY", "UPSET", "HOT", "COLD",
  "YAWNING", "SLEEPING", "NIGHT"
};

/* What LcdTask should draw. Owned by GameTask. */
typedef enum {
  SCREEN_ATTRACT = 0,   // nobody home
  SCREEN_SPLASH,        // "Welcome Back!", held SPLASH_MS
  SCREEN_STATE          // normal per-state readout
} ScreenId;

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

/* Snapshot published for ReportTask and LcdTask. */
typedef struct {
  PetState state;
  ScreenId screen;      // NEW
  bool     present;     // NEW, HC-SR04 presence after hysteresis
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
LiquidCrystal_PCF8574 lcd(LCD_ADDR_PRIMARY);

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

/* Display presence + repaint control. g_lcdPresent stays false if the probe
   in setup() finds nothing, in which case LcdTask idles and the pet runs on
   serial alone -- a missing display must not brick the project. */
static bool g_lcdPresent = false;
static volatile bool g_lcdForceRepaint = true;

/* Cooperative park handshake used before entering light sleep. GameTask sets
   g_parkReq; each other task acknowledges at the top of its loop, waits, then
   re-initializes its own lastWake on release (the FreeRTOS tick is suspended
   during light sleep, so every stored wake time is stale afterwards). */
static volatile bool g_parkReq  = false;
static volatile bool g_parkedSensor = false;
static volatile bool g_parkedSound  = false;
static volatile bool g_parkedInput  = false;
static volatile bool g_parkedLcd    = false;   // NEW
static volatile bool g_parkedReport = false;

/* Cycle counters for measured-frequency verification. */
static volatile uint32_t g_cycSensor = 0;
static volatile uint32_t g_cycSound  = 0;
static volatile uint32_t g_cycInput  = 0;
static volatile uint32_t g_cycGame   = 0;
static volatile uint32_t g_cycLcd    = 0;   // NEW


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

/* Minute-resolution night-window check. Compares time-of-day as
   minutes-since-midnight so the window is not forced to whole-hour
   boundaries, and correctly handles a window that crosses midnight
   (e.g. 22:00 -> 07:00). */
static bool isNightTime(int hour, int minute) {
  int nowMin   = hour * 60 + minute;
  int startMin = NIGHT_START_HOUR * 60 + NIGHT_START_MINUTE;
  int wakeMin  = WAKE_HOUR * 60 + WAKE_MINUTE;
  if (startMin <= wakeMin) {
    return (nowMin >= startMin) && (nowMin < wakeMin);
  }
  return (nowMin >= startMin) || (nowMin < wakeMin);   // window crosses midnight
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


/* ============================ LCD primitives ============================ */
/* Format into a fixed 16-column, space-padded, NUL-terminated line. Padding is
   what lets us drop lcd.clear() entirely: an equal-length overwrite erases the
   previous screen's leftovers with no blank frame and no 2 ms clear() delay. */
static void fmtLine(char dst[LCD_COLS + 1], const char *fmt, ...) {
  char tmp[LCD_COLS + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);   // truncates at 16 by construction
  va_end(ap);

  size_t n = strlen(tmp);
  if (n > LCD_COLS) {
    n = LCD_COLS;
  }
  memcpy(dst, tmp, n);
  for (size_t i = n; i < LCD_COLS; i++) {
    dst[i] = ' ';
  }
  dst[LCD_COLS] = '\0';
}

/* Unconditional write of one already-padded row. Used by LcdTask's diff and by
   enterHibernation() while LcdTask is parked. */
static void lcdWriteRow(uint8_t row, const char *padded) {
  if (!g_lcdPresent) {
    return;
  }
  lcd.setCursor(0, row);
  lcd.print(padded);
}

/* Probe both common backpack addresses on Wire1. Non-fatal by design. */
static bool lcdProbe(uint8_t *foundAddr) {
  const uint8_t candidates[2] = { LCD_ADDR_PRIMARY, LCD_ADDR_ALT };
  for (uint8_t i = 0; i < 2; i++) {
    Wire1.beginTransmission(candidates[i]);
    if (Wire1.endTransmission() == 0) {
      *foundAddr = candidates[i];
      return true;
    }
  }
  return false;
}

/* Build both rows for the current snapshot. Pure function of the snapshot --
   no I2C, no globals -- so it is trivially safe to call from LcdTask. */
static void renderScreen(const StateSnapshot *s,
                         char row0[LCD_COLS + 1],
                         char row1[LCD_COLS + 1]) {
  switch (s->screen) {
    case SCREEN_ATTRACT:
      fmtLine(row0, "Tamagotchi");
      fmtLine(row1, "Come say hi!");
      return;

    case SCREEN_SPLASH:
      fmtLine(row0, "Welcome Back!");
      fmtLine(row1, "%02d:%02d  %.1fC", s->hour, s->minute, s->temp);
      return;

    case SCREEN_STATE:
    default:
      break;
  }

  /* Row 0 is uniform; row 1 is the per-state detail. Every one of the eight
     PetState values is handled, plus a default -- EVERYTHING.ino's switch had
     no HIBERNATING arm, which would have blanked the screen on the single most
     interesting transition in the project. */
  const int si = (int)s->state;
  const char *shortName =
      (si >= 0 && si <= (int)ST_HIBERNATING) ? STATE_SHORT[si] : "???";
  fmtLine(row0, "Pet: %s", shortName);

  switch (s->state) {
    case ST_HAPPY:
      fmtLine(row1, "T:%.1fC H:%.0f%%", s->temp, s->humidity);
      break;

    case ST_HUNGRY:
      fmtLine(row1, "Feed me! (btn)");
      break;

    case ST_OVERSTIMULATED:
      fmtLine(row1, "Too loud! %4d", s->soundLevel);
      break;

    case ST_HOT:
      /* Shows the live temp against the pot-derived band edge, so the pot's
         effect on the control logic is visible on the glass. */
      fmtLine(row1, "%.1fC > %.1fC", s->temp, s->hi);
      break;

    case ST_COLD:
      fmtLine(row1, "%.1fC < %.1fC", s->temp, s->lo);
      break;

    case ST_YAWNING: {
      uint32_t leftMs = (INACTIVITY_TIMEOUT_MS > s->msSinceActivity)
                        ? (INACTIVITY_TIMEOUT_MS - s->msSinceActivity) : 0;
      fmtLine(row1, "Sleepy... %lus", (unsigned long)(leftMs / 1000));
      break;
    }

    case ST_SLEEPING:
      fmtLine(row1, "Zzz     %02d:%02d", s->hour, s->minute);
      break;

    case ST_HIBERNATING:
      fmtLine(row1, "Wake at %02d:00", WAKE_HOUR);
      break;

    default:
      fmtLine(row1, "state %d?", (int)s->state);
      break;
  }
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
   always sees fresh data instead of a stale backlog. Unchanged. */
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
  float   distanceEma     = -1.0f;   // -1 = no valid reading averaged in yet

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
/* HC-SR04 -- rate limited because pulseIn() blocks. EMA-smoothed across
   successive raw readings to damp single-shot ultrasonic noise/bounce.
   A -1.0f timeout is passed straight through and NOT folded into the
   average, and it also resets the filter so a real out-of-range/no-echo
   condition is not masked by stale smoothed history. */
  if (++sonarCounter >= SONAR_EVERY_N_CYCLES) {
  sonarCounter = 0;
  float raw = readDistance();
  if (raw < 0.0f) {
    distanceEma   = -1.0f;
    data.distance = -1.0f;
  } else if (distanceEma < 0.0f) {
    distanceEma   = raw;         // first valid sample, seed the filter
    data.distance = distanceEma;
  } else {
    distanceEma   = distanceEma + DISTANCE_EMA_ALPHA * (raw - distanceEma);
    data.distance = distanceEma;
  }
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
   the smoothed comfort center to qComfort. Unchanged. */
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


/* =============================== LcdTask ================================ */
/* Core 1, lowest priority, 5 Hz. Peeks the GameTask snapshot -- same pattern
   as ReportTask, so a valid copy always remains available and no new queue or
   mutex is needed. Renders into a shadow buffer and writes only the rows that
   actually changed, so holding a state costs no I2C traffic at all while a
   changing value (temperature, clock, sound) still refreshes. That per-cycle
   re-render also fixes a real bug in the source sketch, where the redraw was
   gated on state change alone and row 1's numbers froze on entry to a state. */
static void LcdTask(void *) {
  TickType_t lastWake = xTaskGetTickCount();
  StateSnapshot snap;

  char shadow0[LCD_COLS + 1];
  char shadow1[LCD_COLS + 1];
  char next0[LCD_COLS + 1];
  char next1[LCD_COLS + 1];

  memset(shadow0, 0, sizeof(shadow0));
  memset(shadow1, 0, sizeof(shadow1));

  for (;;) {
    if (parkIfRequested(&g_parkedLcd)) {
      lastWake = xTaskGetTickCount();
    }

    /* Set after a hibernation wake, or the first pass: the HD44780 controller
       cannot be assumed in sync, so invalidate and repaint both rows. */
    if (g_lcdForceRepaint) {
      g_lcdForceRepaint = false;
      memset(shadow0, 0, sizeof(shadow0));
      memset(shadow1, 0, sizeof(shadow1));
    }

    if (g_lcdPresent && xQueuePeek(qStateOut, &snap, 0) == pdTRUE) {
      renderScreen(&snap, next0, next1);

      if (memcmp(shadow0, next0, LCD_COLS) != 0) {
        lcdWriteRow(0, next0);
        memcpy(shadow0, next0, LCD_COLS + 1);
      }
      if (memcmp(shadow1, next1, LCD_COLS) != 0) {
        lcdWriteRow(1, next1);
        memcpy(shadow1, next1, LCD_COLS + 1);
      }
    }

    g_cycLcd++;
    vTaskDelayUntil(&lastWake, LCD_PERIOD);
  }
}


/* ============================== Hibernation ============================= */
/* Arms DS3231 Alarm 1 for WAKE_HOUR:WAKE_MINUTE:00 and enters ESP32-S3
   light sleep. Called from GameTask only. Returns after the chip has
   woken. Uses DS3231_A1_Hour_Minute (matches hour AND minute) instead
   of DS3231_A1_Hour (matches hour only, ignores minutes), so wake fires
   at the exact configured minute, not just the top of the hour. */
static void enterHibernation() {
  Serial.println("[HIB] entering hibernation");
  Serial.flush();

  /* 1. Stop the software timers so they cannot fire a backlog on wake. */
  xTimerStop(tSlowSensor, 0);
  xTimerStop(tHunger, 0);

  /* 2. Park the other tasks at safe points -- never mid-I2C, mid-pulseIn or
        mid-nibble on the LCD. A task frozen halfway through a PCF8574 nibble
        pair leaves the HD44780 out of sync and the screen garbled on wake,
        which is why LcdTask has to be in this handshake. Bounded wait so a
        stuck task cannot deadlock the pet. */
  g_parkReq = true;
  const uint32_t parkDeadline = millis() + 1500;   // ReportTask runs at 1 Hz
  while (millis() < parkDeadline) {
    if (g_parkedSensor && g_parkedSound && g_parkedInput &&
        g_parkedLcd && g_parkedReport) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }

  /* 3. Goodnight screen, then backlight off. Safe to drive the LCD directly
        from here: LcdTask is parked, so the park is the mutual exclusion.
        Killing the backlight is both honest (dark room at 22:00) and worth
        about 20 mA. */
  if (g_lcdPresent) {
    char r0[LCD_COLS + 1];
    char r1[LCD_COLS + 1];
    fmtLine(r0, "Hibernating");
    fmtLine(r1, "Wake at %02d:00", WAKE_HOUR);
    lcdWriteRow(0, r0);
    lcdWriteRow(1, r1);
    lcd.setBacklight(0);
  }

  /* 4. Arm the RTC alarm. SQW must leave square-wave mode or setAlarm1 fails. */
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  rtc.writeSqwPinMode(DS3231_OFF);
  rtc.disableAlarm(2);

  DateTime now  = rtc.now();
  DateTime wake(now.year(), now.month(), now.day(), WAKE_HOUR, WAKE_MINUTE, 0);
  if (wake <= now) {
    wake = wake + TimeSpan(1, 0, 0, 0);   // wake time already passed today
  }
  if (!rtc.setAlarm1(wake, DS3231_A1_Hour)) {
    Serial.println("[HIB] alarm arm FAILED, staying awake");
    if (g_lcdPresent) {
      lcd.setBacklight(LCD_BACKLIGHT_ON);
    }
    g_lcdForceRepaint = true;
    xTimerStart(tSlowSensor, 0);
    xTimerStart(tHunger, 0);
    g_parkReq = false;
    return;
  }

  Serial.printf("[HIB] now %02d:%02d, alarm %02d:00, light sleep now\n",
                now.hour(), now.minute(), wake.hour());
  Serial.flush();

  /* 5. ext0 wake on SQW. Open drain, active LOW, so wake on level 0. */
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  rtc_gpio_pullup_en(PIN_RTC_SQW);
  rtc_gpio_pulldown_dis(PIN_RTC_SQW);
  esp_sleep_enable_ext0_wakeup(PIN_RTC_SQW, 0);

  /* 6. Sleep. RAM, CPU context and peripheral state are preserved, so
        execution continues on the next line after wake. The shadow buffer
        therefore survives, but we invalidate it anyway -- see step 7. */
  esp_light_sleep_start();

  /* ---- awake ---- */
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  rtc.clearAlarm(1);
  rtc.disableAlarm(1);

  /* 7. Backlight back on and force a full repaint. Do not trust the HD44780's
        internal state across a sleep boundary. */
  if (g_lcdPresent) {
    lcd.setBacklight(LCD_BACKLIGHT_ON);
  }
  g_lcdForceRepaint = true;

  xTimerStart(tSlowSensor, 0);
  xTimerStart(tHunger, 0);

  /* 8. Release the parked tasks; each resets its own lastWake. */
  g_parkReq = false;

  Serial.printf("[HIB] woke, cause=%d\n", (int)cause);
}


/* =============================== GameTask =============================== */
/* Core 1. Sole owner of petState AND of the screen state machine. Drains
   Queue #2, reads the latest sensor snapshot from Queue #1, then recomputes a
   single PetState by priority: HIBERNATING > SLEEPING > YAWNING >
   OVERSTIMULATED > HOT/COLD > HUNGRY > HAPPY. Latching conditions (hunger,
   overstimulation) are held in local flags: they are inputs to the state
   computation, not a second state.

   GameTask performs no I2C and touches no display registers in normal
   operation -- it only publishes what to draw. */
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

  /* presence + screen (still used for LCD attract/splash only) */
  bool     present      = false;
  bool     prevPresent  = false;
  uint8_t  presentMiss  = 0;
  ScreenId screen       = SCREEN_ATTRACT;
  uint32_t splashStart  = 0;

  /* sonar motion detection, decoupled from presence (see constants) */
  float    lastDistanceSeen = -1.0f;   // -1 = no valid reading seen yet

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

    /* ---- HC-SR04 presence, hysteresis both ways ----
       distance == -1 is a sonar timeout, i.e. nothing within range, so it
       counts as "far". A few consecutive misses are required before presence
       drops, because a single spurious timeout should not evict the user. */
    if (!present) {
      if (sd.distance > 0.0f && sd.distance <= USER_ENTER_CM) {
        present     = true;
        presentMiss = 0;
      }
    } else {
      const bool far = (sd.distance < 0.0f) || (sd.distance >= USER_EXIT_CM);
      if (far) {
        if (++presentMiss >= PRESENCE_MISSES) {
          present     = false;
          presentMiss = 0;
        }
      } else {
        presentMiss = 0;
      }
    }

    /* ---- activity detection ----
/* ---- activity detection ----
   Presence counts as activity only on its RISING EDGE (someone just
   arrived), not as a continuous level. This is what lets the pet fall
   asleep even while something sits inside the sonar's presence radius
   the whole time -- otherwise idleMs can never grow past one GameTask
   cycle whenever present stays true, which is why the pet was never
   reaching YAWNING/SLEEPING at all.
   A rising edge WHILE ST_YAWNING/ST_SLEEPING still wakes the pet
   immediately, satisfying "wake up via sonar." PIR motion is
   intentionally excluded per the original design rule (telemetry
   only). Sound counts only while the pet is awake -- SLEEPING is
   exited by presence/motion, not by ambient noise alone. */
/* ---- activity detection ----
   Sleep/wake is driven by MOTION and SOUND only. Raw presence
   (something sitting within USER_ENTER_CM) does NOT by itself count --
   a motionless occupant must not block the pet from falling asleep,
   and the pet must be wakeable by movement even without a fresh
   presence-radius crossing.
     - motion  = |current sd.distance - last valid sd.distance| >
                  DISTANCE_MOTION_TOLERANCE_CM, evaluated every cycle,
                  any state (so a moving object wakes a sleeping pet).
     - sound   = sd.soundLevel >= ACTIVITY_SOUND_THRESHOLD, evaluated
                  every cycle including while SLEEPING (per this spec,
                  "loud enough sound" must be able to wake the pet;
                  this intentionally removes the old "sound excluded
                  while SLEEPING" guard, since it contradicted the
                  requested behavior). */
    bool distanceMotion = false;
    if (sd.distance >= 0.0f) {
      if (lastDistanceSeen >= 0.0f) {
        float delta = fabsf(sd.distance - lastDistanceSeen);
        if (delta > DISTANCE_MOTION_TOLERANCE_CM) {
          distanceMotion = true;
        }
      }
      lastDistanceSeen = sd.distance;
    }
    /* A sonar timeout (-1) is treated as "no reading," not as motion and
      not as stillness -- it simply skips this cycle's motion check. */

    bool loudEnough = (sd.soundLevel >= ACTIVITY_SOUND_THRESHOLD);

    if (distanceMotion || loudEnough) {
      activity = true;
    }
    if (activity) {
      lastActivityMs = now;
    }
    const uint32_t idleMs = now - lastActivityMs;

    /* ---- screen state machine ----
       Rising edge of presence triggers the splash; the splash hands off to the
       state screen after SPLASH_MS. Unlike the source sketch this is an
       explicit state, not an implicit millis() comparison against a
       zero-initialized timestamp. */
    if (screen == SCREEN_ATTRACT) {
      if (present && !prevPresent) {
        screen      = SCREEN_SPLASH;
        splashStart = now;
#if VERBOSE_TRANSITIONS
        Serial.println("[UI] user detected -> SPLASH");
#endif
      }
    } else if (screen == SCREEN_SPLASH) {
      if ((now - splashStart) >= SPLASH_MS) {
        screen = SCREEN_STATE;
      }
    } else {  /* SCREEN_STATE */
#if RETURN_TO_ATTRACT_ON_EXIT
      if (!present) {
        screen = SCREEN_ATTRACT;
#if VERBOSE_TRANSITIONS
        Serial.println("[UI] user left -> ATTRACT");
#endif
      }
#endif
    }
    prevPresent = present;

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
    if (haveSensor && isNightTime(sd.hour, sd.minute) && now >= hibBlockUntilMs) {
      state = ST_HIBERNATING;

      /* Publish the hibernating state first so ReportTask's last snapshot is
         truthful, then sleep. */
      StateSnapshot hibSnap;
      hibSnap.state           = state;
      hibSnap.screen          = SCREEN_STATE;
      hibSnap.present         = present;
      hibSnap.temp            = sd.temp;
      hibSnap.humidity        = sd.humidity;
      hibSnap.distance        = sd.distance;
      hibSnap.center          = center;
      hibSnap.lo              = lo;
      hibSnap.hi              = hi;
      hibSnap.soundLevel      = sd.soundLevel;
      hibSnap.motion          = sd.motion;
      hibSnap.hour            = sd.hour;
      hibSnap.minute          = sd.minute;
      hibSnap.second          = sd.second;
      hibSnap.hungry          = hungryLatched;
      hibSnap.overstim        = overstimLatched;
      hibSnap.hot             = hot;
      hibSnap.cold            = cold;
      hibSnap.msSinceActivity = idleMs;
      xQueueOverwrite(qStateOut, &hibSnap);

      enterHibernation();

      /* Fresh start after wake: tick count moved, so re-seed everything. */
      lastWake         = xTaskGetTickCount();
      lastActivityMs   = millis();
      hibBlockUntilMs  = millis() + POST_WAKE_BLOCK_MS;
      state            = ST_HAPPY;
      prevState        = ST_HIBERNATING;
      present          = false;
      prevPresent      = false;
      presentMiss      = 0;
      screen           = SCREEN_ATTRACT;
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

    /* ---- publish snapshot for ReportTask and LcdTask ---- */
    StateSnapshot snap;
    snap.state           = state;
    snap.screen          = screen;
    snap.present         = present;
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
   task frequencies, which is the only honest way to check the 100/10/5 Hz
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
        "(center %.1f) | snd %4d | dist %6.1fcm | PIR %d | PRES %d | "
        "SCR %d | HOT%d COLD%d OVER%d HUNGRY%d | idle %lus\n",
        STATE_NAME[snap.state],
        snap.hour, snap.minute, snap.second,
        snap.temp, snap.humidity,
        snap.lo, snap.hi, snap.center,
        snap.soundLevel, snap.distance, snap.motion ? 1 : 0,
        snap.present ? 1 : 0, (int)snap.screen,
        snap.hot ? 1 : 0, snap.cold ? 1 : 0,
        snap.overstim ? 1 : 0, snap.hungry ? 1 : 0,
        (unsigned long)(snap.msSinceActivity / 1000));
    }

    const uint32_t now = millis();
    if ((now - lastRateMs) >= RATE_WINDOW_MS) {
      const uint32_t sc = g_cycSensor, so = g_cycSound;
      const uint32_t ic = g_cycInput,  gc = g_cycGame, lc = g_cycLcd;
      g_cycSensor = 0; g_cycSound = 0; g_cycInput = 0;
      g_cycGame = 0;   g_cycLcd = 0;
      const float win = (float)(now - lastRateMs);
      Serial.printf(
        "[RATE] Sensor %.1f Hz | Sound %.1f Hz | Input %.1f Hz | "
        "Game %.1f Hz | Lcd %.1f Hz\n",
        sc * 1000.0f / win, so * 1000.0f / win,
        ic * 1000.0f / win, gc * 1000.0f / win,
        lc * 1000.0f / win);
      lastRateMs = now;
    }

    vTaskDelayUntil(&lastWake, REPORT_PERIOD);
  }
}


/* ================================ setup ================================= */
void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== Tamagotchi Pet: final, sensors + input + LCD ===");

  /* --- pin modes --- unchanged --- */
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  digitalWrite(PIN_TRIG, LOW);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_BTN_FEED, INPUT_PULLUP);
  pinMode(PIN_IR_CALM, INPUT);
  pinMode(PIN_RTC_SQW, INPUT_PULLUP);   // DS3231 SQW is open drain
  analogReadResolution(12);
  /* DHT11 and the analog sound module need no manual pinMode. The LCD needs
     none either -- Wire1.begin() configures GPIO 17/18. */

  /* --- I2C bus 0: DS3231 only, core 0 --- */
  Wire.begin(PIN_SDA, PIN_SCL);
  if (!rtc.begin()) {
    Serial.println("FATAL: DS3231 not found");
    for (;;) { delay(1000); }
  }
  Serial.println("Resyncing RTC to build timestamp");
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  /* Leave SQW as a plain interrupt line and clear anything stale. */
  rtc.writeSqwPinMode(DS3231_OFF);
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  rtc.disableAlarm(1);
  rtc.disableAlarm(2);

  /* Clock set AFTER rtc.begin(): RTClib re-calls Wire.begin() internally, and
     setting the rate last means nothing downstream can stomp on it. */
  Wire.setClock(400000);                // DS3231 is a 400 kHz part

  dht.begin();

  /* --- I2C bus 1: LCD only, core 1 --- NEW ---
     A separate controller on separate pins, so the display is never touched
     from the same bus as the RTC and no I2C mutex is needed anywhere. */
  Wire1.begin(PIN_LCD_SDA, PIN_LCD_SCL);

  uint8_t lcdAddr = LCD_ADDR_PRIMARY;
  if (lcdProbe(&lcdAddr)) {
    /* Rebind to whichever address answered, then hand the library Wire1. */
    lcd = LiquidCrystal_PCF8574(lcdAddr);
    lcd.begin(LCD_COLS, LCD_ROWS, Wire1);
    lcd.setBacklight(LCD_BACKLIGHT_ON);
    lcd.clear();                        // the one legitimate clear(), at boot
    g_lcdPresent      = true;
    g_lcdForceRepaint = true;

    char r0[LCD_COLS + 1];
    char r1[LCD_COLS + 1];
    fmtLine(r0, "Tamagotchi");
    fmtLine(r1, "Starting...");
    lcdWriteRow(0, r0);
    lcdWriteRow(1, r1);

    Serial.printf("LCD found at 0x%02X on Wire1 (SDA %u, SCL %u)\n",
                  lcdAddr, PIN_LCD_SDA, PIN_LCD_SCL);

    /* Same reasoning as bus 0: pin the rate down after the library's own
       begin() has run. PCF8574 is a standard-mode part, so 100 kHz. */
    Wire1.setClock(100000);
  } else {
    /* Non-fatal on purpose: a missing display degrades to serial-only. */
    g_lcdPresent = false;
    Serial.println("WARN: no LCD at 0x27 or 0x3F on Wire1, "
                   "continuing serial-only");
  }

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
  xTaskCreatePinnedToCore(LcdTask,    "LcdTask",    4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(ReportTask, "ReportTask", 8192, NULL, 1, NULL, 1);

  Serial.println("setup complete, RTOS running");
}


void loop() {
  /* Everything runs in tasks. */
}
