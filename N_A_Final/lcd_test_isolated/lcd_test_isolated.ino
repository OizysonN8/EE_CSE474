/* ============================================================================
 * lcd_test_isolated.ino
 *
 * Standalone extraction of the LCD subsystem from tamagotchi_main_final.ino,
 * for testing the 16x2 HD44780 + PCF8574 backpack IN ISOLATION, with no
 * FreeRTOS tasks, no sensors, and no queues. Same library, same pin
 * assignments, same I2C bus (Wire1), same constants, and the same
 * fmtLine()/lcdWriteRow()/renderScreen() shadow-buffer pattern as the main
 * sketch, so behavior you see here should carry over directly.
 *
 * LIBRARY (must match main sketch exactly):
 *   LiquidCrystal_PCF8574 (Matthias Hertel) -- NOT LiquidCrystal_I2C.
 *   Install via Library Manager: "LiquidCrystal_PCF8574".
 *   Reason from the main sketch: LiquidCrystal_I2C is hardwired to the
 *   global Wire object and cannot be pointed at Wire1.
 *
 * WIRING (unchanged from tamagotchi_main_final.ino):
 *   SDA -> GPIO 17  (Wire1)
 *   SCL -> GPIO 18  (Wire1)
 *   VCC -> 5V, GND -> GND
 *   NOTE (from main sketch): the backpack's onboard pull-ups will pull
 *   SDA/SCL to 5V, and the ESP32-S3 is NOT 5V tolerant. Either lift the
 *   backpack's pull-ups and add 4.7k pull-ups to 3V3, use a level shifter,
 *   or run the backpack at 3.3V. If the screen is blank/solid black, turn
 *   the contrast trimpot before assuming firmware is broken.
 *
 * WHAT THIS FILE DOES
 *   Cycles through the same ScreenId/PetState screens the real GameTask
 *   would hand to LcdTask, using fabricated StateSnapshot values instead of
 *   live sensor data, so you can verify the display, the PCF8574 address,
 *   and the shadow-buffer diff logic before wiring it back into the main
 *   sketch's LcdTask.
 * ========================================================================== */

#include <Arduino.h>
#include <stdarg.h>
#include <string.h>
#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>

/* --- display pins, unchanged from tamagotchi_main_final.ino --- */
static const uint8_t PIN_LCD_SDA  = 17;   // Wire1, PCF8574 backpack
static const uint8_t PIN_LCD_SCL  = 18;   // Wire1, PCF8574 backpack

/* --- display constants, unchanged from tamagotchi_main_final.ino --- */
static const uint8_t  LCD_COLS         = 16;
static const uint8_t  LCD_ROWS         = 2;
static const uint8_t  LCD_ADDR_PRIMARY = 0x27;  // most backpacks
static const uint8_t  LCD_ADDR_ALT     = 0x3F;  // the other common one
static const uint8_t  LCD_BACKLIGHT_ON = 255;
static const uint32_t SPLASH_MS        = 2000;  // "Welcome Back!" hold

/* Local stand-in for the LCD_PERIOD used by the real LcdTask (200 ms / 5 Hz). */
static const uint32_t LCD_PERIOD_MS = 200;

/* --- PetState / ScreenId / StateSnapshot, trimmed to what rendering needs --- */
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

typedef enum {
  SCREEN_ATTRACT = 0,   // nobody home
  SCREEN_SPLASH,        // "Welcome Back!", held SPLASH_MS
  SCREEN_STATE          // normal per-state readout
} ScreenId;

typedef struct {
  PetState state;
  ScreenId screen;
  bool     present;
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

/* --- globals, same object type/construction as the main sketch --- */
LiquidCrystal_PCF8574 lcd(LCD_ADDR_PRIMARY);
static bool g_lcdPresent = false;

/* ============================ LCD primitives ============================
 * Copied verbatim (pattern-for-pattern) from tamagotchi_main_final.ino so
 * behavior matches exactly when this code is folded back into LcdTask. */

/* Format into a fixed 16-column, space-padded, NUL-terminated line. Padding
   is what lets us drop lcd.clear() entirely: an equal-length overwrite
   erases the previous screen's leftovers with no blank frame and no 2 ms
   clear() delay. */
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

/* Unconditional write of one already-padded row. */
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
   no I2C, no globals -- matches the main sketch's renderScreen(). The
   per-state row1 detail lines below are a reasonable reconstruction of the
   main sketch's intent (short state name + one relevant reading per state);
   confirm against your actual renderScreen() switch body before assuming
   these strings are byte-identical to production, then paste the real
   switch block in if you want an exact match. */
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

  fmtLine(row0, "Pet: %s", STATE_NAME[s->state]);

  switch (s->state) {
    case ST_HAPPY:
      fmtLine(row1, "%02d:%02d:%02d  %.1fC", s->hour, s->minute, s->second, s->temp);
      break;
    case ST_HUNGRY:
      fmtLine(row1, "Feed me! %.1fC", s->temp);
      break;
    case ST_OVERSTIMULATED:
      fmtLine(row1, "Sound=%d", s->soundLevel);
      break;
    case ST_HOT:
      fmtLine(row1, "%.1fC > %.1fC", s->temp, s->hi);
      break;
    case ST_COLD:
      fmtLine(row1, "%.1fC < %.1fC", s->temp, s->lo);
      break;
    case ST_YAWNING:
      fmtLine(row1, "Getting sleepy");
      break;
    case ST_SLEEPING:
      fmtLine(row1, "Zzz...");
      break;
    case ST_HIBERNATING:
      fmtLine(row1, "Goodnight");
      break;
    default:
      fmtLine(row1, "");
      break;
  }
}

/* ============================ Test harness ===============================
 * Replaces LcdTask's FreeRTOS loop with a plain loop() that cycles through
 * screens every LCD_PERIOD_MS-derived interval, using the same shadow-buffer
 * diff so you're testing the exact write pattern the real task uses. */

static char shadow0[LCD_COLS + 1];
static char shadow1[LCD_COLS + 1];
static char next0[LCD_COLS + 1];
static char next1[LCD_COLS + 1];

static uint32_t lastStep = 0;
static uint8_t  demoIndex = 0;
static const uint32_t STEP_MS = 2500;  // hold each demo screen this long

static StateSnapshot buildDemoSnapshot(uint8_t idx) {
  StateSnapshot s;
  memset(&s, 0, sizeof(s));
  s.hour = 14; s.minute = 30; s.second = (idx * 7) % 60;
  s.temp = 22.5f + idx; s.humidity = 45.0f;
  s.distance = 40.0f; s.center = 22.0f; s.lo = 18.5f; s.hi = 25.5f;
  s.soundLevel = 80 + idx * 20;

  switch (idx) {
    case 0: s.screen = SCREEN_ATTRACT; s.state = ST_HAPPY; break;
    case 1: s.screen = SCREEN_SPLASH;  s.state = ST_HAPPY; break;
    case 2: s.screen = SCREEN_STATE;   s.state = ST_HAPPY; break;
    case 3: s.screen = SCREEN_STATE;   s.state = ST_HUNGRY; break;
    case 4: s.screen = SCREEN_STATE;   s.state = ST_OVERSTIMULATED; break;
    case 5: s.screen = SCREEN_STATE;   s.state = ST_HOT; break;
    case 6: s.screen = SCREEN_STATE;   s.state = ST_COLD; break;
    case 7: s.screen = SCREEN_STATE;   s.state = ST_YAWNING; break;
    case 8: s.screen = SCREEN_STATE;   s.state = ST_SLEEPING; break;
    default: s.screen = SCREEN_STATE;  s.state = ST_HIBERNATING; break;
  }
  return s;
}

static const uint8_t DEMO_SCREEN_COUNT = 9;

void setup() {
  Serial.begin(115200);
  delay(200);

  /* Same bus and pins as tamagotchi_main_final.ino: Wire1 on GPIO 17/18,
     kept separate from the DS3231's Wire on GPIO 6/7 so no I2C mutex is
     needed between cores in the full project. */
  Wire1.begin(PIN_LCD_SDA, PIN_LCD_SCL);

  uint8_t foundAddr = LCD_ADDR_PRIMARY;
  g_lcdPresent = lcdProbe(&foundAddr);

  if (g_lcdPresent) {
    lcd = LiquidCrystal_PCF8574(foundAddr);
    lcd.begin(LCD_COLS, LCD_ROWS, Wire1);
    lcd.setBacklight(LCD_BACKLIGHT_ON);
    Serial.printf("[LCD] found at 0x%02X\n", foundAddr);
  } else {
    Serial.println("[LCD] not found on Wire1 (0x27 / 0x3F) -- check wiring/contrast");
  }

  memset(shadow0, 0, sizeof(shadow0));
  memset(shadow1, 0, sizeof(shadow1));
  lastStep = millis();
}

void loop() {
  uint32_t now = millis();
  if (now - lastStep >= STEP_MS) {
    lastStep = now;
    demoIndex = (demoIndex + 1) % DEMO_SCREEN_COUNT;
  }

  StateSnapshot snap = buildDemoSnapshot(demoIndex);
  renderScreen(&snap, next0, next1);

  if (memcmp(shadow0, next0, LCD_COLS) != 0) {
    lcdWriteRow(0, next0);
    memcpy(shadow0, next0, LCD_COLS + 1);
    Serial.printf("[LCD] row0: \"%s\"\n", next0);
  }
  if (memcmp(shadow1, next1, LCD_COLS) != 0) {
    lcdWriteRow(1, next1);
    memcpy(shadow1, next1, LCD_COLS + 1);
    Serial.printf("[LCD] row1: \"%s\"\n", next1);
  }

  delay(LCD_PERIOD_MS);
}
