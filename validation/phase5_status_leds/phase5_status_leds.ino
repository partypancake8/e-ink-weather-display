/*
 * ============================================================
 * Phase 5 — Full System + NeoPixel Status LED
 * Adafruit ESP32-S3 Feather  +  SHT45  +  LiPo  +  Wi-Fi
 * ============================================================
 *
 * Onboard NeoPixel: GPIO 33, powered via GPIO 21 (NEOPIXEL_POWER)
 * Both are defined in the board variant — no manual pin edits needed.
 *
 * LED status key:
 * ┌──────────────────────────────────────────────────────┐
 * │  WHITE  slow breathe  │  Booting / initialising      │
 * │  YELLOW fast blink    │  Connecting to Wi-Fi          │
 * │  GREEN  solid 1 s     │  Wi-Fi just connected         │
 * │  CYAN   brief flash   │  Sensor read fired (healthy)  │
 * │  ORANGE slow blink    │  Wi-Fi lost, reconnecting     │
 * │  RED    fast blink    │  Battery critical (< 10 %)    │
 * │  PURPLE dim pulse     │  Battery low (10–20 %)        │
 * └──────────────────────────────────────────────────────┘
 *
 * Libraries (install via Sketch > Include Library > Manage Libraries):
 *   1. "Adafruit NeoPixel"     by Adafruit  (search: NeoPixel)
 *   2. "Adafruit SHT4x Library"  by Adafruit
 *   3. "Adafruit MAX1704X"      by Adafruit
 *   (WiFi.h bundled with ESP32 core)
 *
 * Arduino IDE settings:
 *   Board           : Adafruit Feather ESP32-S3 2MB PSRAM
 *   USB CDC On Boot : Enabled
 * ============================================================
 */

#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_MAX1704X.h>

// ================================================================
// Wi-Fi credentials — edit before flashing
// ================================================================
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";
// ================================================================

static const uint32_t READ_INTERVAL_MS  = 5000;
static const float    BATT_LOW_PCT      = 20.0f;
static const float    BATT_CRIT_PCT     = 10.0f;

// ---- NeoPixel ----
// PIN_NEOPIXEL (33) and NEOPIXEL_POWER (21) are defined in the
// Adafruit ESP32-S3 board variant — no hardcoding needed.
Adafruit_NeoPixel pixel(NEOPIXEL_NUM, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

// ---- Sensors ----
Adafruit_SHT4x    sht4;
Adafruit_MAX17048 maxlipo;

// ================================================================
// LED state machine
// ================================================================
enum LedState {
  LED_BOOTING,        // white slow breathe
  LED_WIFI_CONNECTING,// yellow fast blink
  LED_WIFI_CONNECTED, // green solid (1 s), then transitions to LED_HEALTHY
  LED_HEALTHY,        // cyan flash on read, off between reads
  LED_WIFI_LOST,      // orange slow blink
  LED_BATT_LOW,       // dim purple slow pulse
  LED_BATT_CRITICAL   // red fast blink
};

static LedState  ledState       = LED_BOOTING;
static uint32_t  ledTimer       = 0;
static bool      ledPhase       = false;   // generic toggle for blink states
static uint8_t   breatheVal     = 0;
static int8_t    breatheDir     = 1;

// Flash the pixel with a given color for a duration, non-blocking.
// Call updateLed() in loop to process state.
static bool      flashPending   = false;
static uint32_t  flashColor     = 0;
static uint32_t  flashEnd       = 0;

// ---- helpers ----
static inline uint32_t color(uint8_t r, uint8_t g, uint8_t b) {
  return pixel.Color(r, g, b);
}

void setLedState(LedState s) {
  ledState = s;
  ledTimer = millis();
  ledPhase = false;
}

// Call once per loop() — drives all non-blocking LED behaviour.
void updateLed() {
  uint32_t now = millis();

  // A brief flash (e.g. sensor read) overrides the current state temporarily.
  if (flashPending) {
    if (now < flashEnd) {
      pixel.setPixelColor(0, flashColor);
      pixel.show();
      return;
    }
    flashPending = false;
  }

  switch (ledState) {

    // ---- White slow breathe ----
    case LED_BOOTING: {
      if (now - ledTimer >= 16) {  // ~60 fps
        ledTimer = now;
        breatheVal += breatheDir * 3;
        if (breatheVal >= 120) { breatheVal = 120; breatheDir = -1; }
        if (breatheVal == 0)   { breatheDir =  1; }
        uint8_t v = breatheVal;
        pixel.setPixelColor(0, color(v, v, v));
        pixel.show();
      }
      break;
    }

    // ---- Yellow fast blink (200 ms on/off) ----
    case LED_WIFI_CONNECTING: {
      if (now - ledTimer >= 200) {
        ledTimer = now;
        ledPhase = !ledPhase;
        pixel.setPixelColor(0, ledPhase ? color(180, 120, 0) : color(0, 0, 0));
        pixel.show();
      }
      break;
    }

    // ---- Solid green for 1 s, then auto-switch to HEALTHY ----
    case LED_WIFI_CONNECTED: {
      pixel.setPixelColor(0, color(0, 180, 0));
      pixel.show();
      if (now - ledTimer >= 1000) {
        setLedState(LED_HEALTHY);
      }
      break;
    }

    // ---- Healthy: dim green heartbeat (off between flashes) ----
    case LED_HEALTHY: {
      // Dim heartbeat: 80 ms on, 920 ms off
      if (!ledPhase && now - ledTimer >= 920) {
        ledTimer = now;
        ledPhase = true;
        pixel.setPixelColor(0, color(0, 40, 0));
        pixel.show();
      } else if (ledPhase && now - ledTimer >= 80) {
        ledTimer = now;
        ledPhase = false;
        pixel.setPixelColor(0, color(0, 0, 0));
        pixel.show();
      }
      break;
    }

    // ---- Orange slow blink (600 ms on/off) ----
    case LED_WIFI_LOST: {
      if (now - ledTimer >= 600) {
        ledTimer = now;
        ledPhase = !ledPhase;
        pixel.setPixelColor(0, ledPhase ? color(200, 60, 0) : color(0, 0, 0));
        pixel.show();
      }
      break;
    }

    // ---- Dim purple slow pulse ----
    case LED_BATT_LOW: {
      if (now - ledTimer >= 20) {
        ledTimer = now;
        breatheVal += breatheDir * 2;
        if (breatheVal >= 80) { breatheVal = 80; breatheDir = -1; }
        if (breatheVal == 0)  { breatheDir =  1; }
        uint8_t v = breatheVal;
        pixel.setPixelColor(0, color(v / 2, 0, v));  // purple
        pixel.show();
      }
      break;
    }

    // ---- Red fast blink (150 ms on/off) ----
    case LED_BATT_CRITICAL: {
      if (now - ledTimer >= 150) {
        ledTimer = now;
        ledPhase = !ledPhase;
        pixel.setPixelColor(0, ledPhase ? color(220, 0, 0) : color(0, 0, 0));
        pixel.show();
      }
      break;
    }
  }
}

// Queue a brief cyan flash for the next updateLed() call (non-blocking, 120 ms).
void flashReadIndicator() {
  flashColor   = color(0, 160, 140);  // cyan
  flashEnd     = millis() + 120;
  flashPending = true;
}

// ================================================================
// Subsystem init
// ================================================================

void initNeoPixel() {
  // NEOPIXEL_POWER must be driven HIGH before the pixel will light.
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
  delay(5);

  pixel.begin();
  pixel.setBrightness(80);  // 0–255; 80 is comfortable indoors
  pixel.clear();
  pixel.show();
  setLedState(LED_BOOTING);
}

void initSHT45() {
  Serial.print("[SHT45]   ");
  if (!sht4.begin()) {
    Serial.println("NOT FOUND. Halting.");
    while (1) {
      pixel.setPixelColor(0, color(255, 0, 0));
      pixel.show(); delay(200);
      pixel.clear(); pixel.show(); delay(200);
    }
  }
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);
  Serial.println("OK  (HIGH precision, heater OFF)");
}

void initBattery() {
  Serial.print("[BATTERY] ");
  if (!maxlipo.begin()) {
    Serial.println("MAX17048 NOT FOUND. Halting.");
    while (1) {
      pixel.setPixelColor(0, color(255, 0, 0));
      pixel.show(); delay(200);
      pixel.clear(); pixel.show(); delay(200);
    }
  }
  Serial.print("OK  (MAX17048, ID: 0x");
  Serial.print(maxlipo.getChipID(), HEX);
  Serial.println(")");
}

void connectWiFi() {
  Serial.print("[WI-FI]   Connecting to \"");
  Serial.print(WIFI_SSID);
  Serial.print("\" ");

  setLedState(LED_WIFI_CONNECTING);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    updateLed();  // keep LED blinking while we wait
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WI-FI]   Connected.   IP  : ");
    Serial.println(WiFi.localIP());
    Serial.print("[WI-FI]               RSSI : ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    setLedState(LED_WIFI_CONNECTED);
  } else {
    Serial.println("[WI-FI]   FAILED. Continuing without Wi-Fi.");
    setLedState(LED_WIFI_LOST);
  }
}

// ================================================================

void setup() {
  Serial.begin(115200);
  unsigned long t = millis();
  while (!Serial && (millis() - t) < 3000) delay(10);

  initNeoPixel();  // first: gives visual feedback during boot
  Wire.begin();

  Serial.println();
  Serial.println("================================================");
  Serial.println("  ESP32-S3 Feather — Phase 5 (Status LEDs)");
  Serial.println("================================================");
  Serial.print("  Firmware built: ");
  Serial.println(__DATE__ " " __TIME__);
  Serial.println("------------------------------------------------");

  initSHT45();
  initBattery();
  connectWiFi();

  Serial.println("------------------------------------------------");
  Serial.println("  All systems go.");
  Serial.print("  Reading every ");
  Serial.print(READ_INTERVAL_MS / 1000);
  Serial.println(" s.");
  Serial.println("================================================");
  Serial.println();
}

// ================================================================

void loop() {
  static uint32_t lastRead = 0;
  uint32_t now = millis();

  // ---- Non-blocking LED update (runs every loop iteration) ----
  updateLed();

  // ---- Sensor + print every READ_INTERVAL_MS ----
  if (now - lastRead >= READ_INTERVAL_MS) {
    lastRead = now;

    // --- SHT45 ---
    sensors_event_t humid_ev, temp_ev;
    sht4.getEvent(&humid_ev, &temp_ev);
    float tempF   = (temp_ev.temperature * 9.0f / 5.0f) + 32.0f;
    float rh      = humid_ev.relative_humidity;

    // --- Battery ---
    float battV   = maxlipo.cellVoltage();
    float battPct = constrain(maxlipo.cellPercent(), 0.0f, 100.0f);

    // --- Wi-Fi reconnect ---
    bool wifiUp = (WiFi.status() == WL_CONNECTED);
    if (!wifiUp) {
      WiFi.reconnect();
    }

    // ---- Update LED state based on current conditions ----
    // Priority: critical battery > low battery > wifi lost > healthy
    if (battPct < BATT_CRIT_PCT) {
      if (ledState != LED_BATT_CRITICAL) setLedState(LED_BATT_CRITICAL);
    } else if (battPct < BATT_LOW_PCT) {
      if (ledState != LED_BATT_LOW)      setLedState(LED_BATT_LOW);
    } else if (!wifiUp) {
      if (ledState != LED_WIFI_LOST)     setLedState(LED_WIFI_LOST);
    } else {
      // Battery OK + Wi-Fi up: ensure we're in HEALTHY (or WIFI_CONNECTED transition)
      if (ledState == LED_WIFI_LOST)     setLedState(LED_WIFI_CONNECTED);
      if (ledState == LED_HEALTHY)       flashReadIndicator();  // cyan pulse on read
    }

    // ---- Serial output ----
    static uint32_t readingNum = 0;
    readingNum++;

    Serial.print("=== Reading #");
    Serial.print(readingNum);
    Serial.print("  (uptime: ");
    Serial.print(now / 1000UL);
    Serial.println(" s) ===");

    Serial.print("  Temperature  : ");
    Serial.print(tempF, 1);
    Serial.println(" F");

    Serial.print("  Humidity     : ");
    Serial.print(rh, 1);
    Serial.println(" %RH");

    Serial.print("  Batt Voltage : ");
    Serial.print(battV, 3);
    Serial.println(" V");

    Serial.print("  Batt SOC     : ");
    Serial.print(battPct, 1);
    Serial.print(" %");

    if      (battPct < BATT_CRIT_PCT) Serial.println("  [CRITICAL]");
    else if (battPct < BATT_LOW_PCT)  Serial.println("  [LOW]");
    else                              Serial.println();

    if (wifiUp) {
      Serial.print("  Wi-Fi        : Connected  |  IP: ");
      Serial.print(WiFi.localIP());
      Serial.print("  |  RSSI: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
    } else {
      Serial.println("  Wi-Fi        : Not connected  (reconnecting...)");
    }

    Serial.println();
  }
}
