/*
 * ============================================================
 * Phase 4 — Full System Validation (Final Sketch)
 * Adafruit ESP32-S3 Feather  +  SHT45  +  LiPo  +  Wi-Fi
 * ============================================================
 *
 * Libraries (install via Sketch > Include Library > Manage Libraries):
 *   1. "Adafruit SHT4x Library"    by Adafruit  (search: SHT4x)
 *      -> Accept "Install all" to get BusIO + Unified Sensor
 *   2. "Adafruit MAX1704X"          by Adafruit  (search: MAX1704X)
 *   3. WiFi.h — part of ESP32 Arduino core, no separate install
 *
 * Arduino IDE settings:
 *   Board             : Adafruit Feather ESP32-S3 No PSRAM
 *                       (use "...4MB Flash 2MB PSRAM" variant if yours has PSRAM)
 *   USB CDC On Boot   : Enabled  <-- Required for Serial over USB-C
 *   Upload Speed      : 921600
 *   CPU Frequency     : 240 MHz
 *   Port              : /dev/cu.usbmodem...  (appears after reset)
 *
 * Battery-only validation (Phase 4 goal):
 *   1. Flash this sketch with USB connected and confirm all readings.
 *   2. Disconnect USB. The sketch keeps running on LiPo.
 *   3. Reconnect USB after 30–60 s.
 *   4. Open Serial Monitor. The uptime counter (shown each reading)
 *      and reading number prove the MCU ran uninterrupted.
 *   Note: Serial output is not visible while USB is disconnected.
 *         That is normal — the CDC port only exists over USB.
 * ============================================================
 */

#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_MAX1704X.h>

// ================================================================
// Configuration — edit these before flashing
// ================================================================
const char* WIFI_SSID       = "YOUR_SSID";
const char* WIFI_PASSWORD   = "YOUR_PASSWORD";

static const uint32_t READ_INTERVAL_MS = 5000; // ms between sensor polls
// ================================================================

Adafruit_SHT4x    sht4;
Adafruit_MAX17048 maxlipo;

// ---- forward declarations ----
void initSHT45();
void initBattery();
void connectWiFi();
void printReadings();

// ================================================================

void setup() {
  Serial.begin(115200);
  // Timeout-based wait: safe on battery (won't hang with no PC attached)
  unsigned long t = millis();
  while (!Serial && (millis() - t) < 3000) delay(10);

  Wire.begin();

  Serial.println();
  Serial.println("============================================");
  Serial.println("  ESP32-S3 Feather — Full System Validation");
  Serial.println("============================================");
  Serial.print("  Firmware built: ");
  Serial.println(__DATE__ " " __TIME__);
  Serial.println("--------------------------------------------");

  initSHT45();
  initBattery();
  connectWiFi();

  Serial.println("--------------------------------------------");
  Serial.println("  All systems go.");
  Serial.print("  Reading every ");
  Serial.print(READ_INTERVAL_MS / 1000);
  Serial.println(" s.");
  Serial.println("============================================");
  Serial.println();
}

// ================================================================

void loop() {
  printReadings();
  delay(READ_INTERVAL_MS);
}

// ================================================================
// Subsystem init functions
// ================================================================

void initSHT45() {
  Serial.print("[SHT45]   Initializing... ");
  if (!sht4.begin()) {
    Serial.println("NOT FOUND.");
    Serial.println("          -> Check STEMMA QT cable and seating.");
    Serial.println("          -> Run phase0_i2c_scan; expect 0x44.");
    while (1) delay(10); // halt: sensor is mandatory
  }
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);
  Serial.println("OK  (HIGH precision, heater OFF)");
}

// ================================================================

void initBattery() {
  Serial.print("[BATTERY] Initializing... ");
  if (!maxlipo.begin()) {
    Serial.println("MAX17048 NOT FOUND.");
    Serial.println("          -> Is a LiPo connected to the JST-PH port?");
    Serial.println("          -> Run phase0_i2c_scan; expect 0x36.");
    while (1) delay(10); // halt: battery telemetry is mandatory for this phase
  }
  Serial.print("OK  (MAX17048, chip ID: 0x");
  Serial.print(maxlipo.getChipID(), HEX);
  Serial.println(")");
  Serial.println("          NOTE: SOC% accuracy improves after 1-2 full");
  Serial.println("          charge/discharge cycles (MAX17048 cell modeling).");
}

// ================================================================

void connectWiFi() {
  Serial.print("[WI-FI]   Connecting to \"");
  Serial.print(WIFI_SSID);
  Serial.print("\" ");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // 40 × 500 ms = 20 s timeout
  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WI-FI]   Connected.   IP   : ");
    Serial.println(WiFi.localIP());
    Serial.print("[WI-FI]               SSID  : ");
    Serial.println(WiFi.SSID());
    Serial.print("[WI-FI]               RSSI  : ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("[WI-FI]   FAILED. Continuing without Wi-Fi.");
    Serial.println("          -> Verify WIFI_SSID and WIFI_PASSWORD.");
    Serial.println("          -> ESP32 is 2.4 GHz only; check router band.");
    // Non-fatal: sensor and battery readings still work.
  }
}

// ================================================================
// Main read-and-print function
// ================================================================

void printReadings() {
  static uint32_t readingNum = 0;
  readingNum++;

  // --- SHT45 ---
  sensors_event_t humid_ev, temp_ev;
  sht4.getEvent(&humid_ev, &temp_ev); // blocks ~8 ms at HIGH precision

  float tempF = (temp_ev.temperature * 9.0f / 5.0f) + 32.0f;
  float rh    = humid_ev.relative_humidity;

  // --- MAX17048 ---
  float battV   = maxlipo.cellVoltage();
  float battPct = constrain(maxlipo.cellPercent(), 0.0f, 100.0f);

  // --- Wi-Fi ---
  bool wifiUp = (WiFi.status() == WL_CONNECTED);
  // Attempt silent reconnect if the link dropped (e.g., AP restarted)
  if (!wifiUp) WiFi.reconnect();

  // --- Print ---
  Serial.print("=== Reading #");
  Serial.print(readingNum);
  Serial.print("  (uptime: ");
  Serial.print(millis() / 1000UL);
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
  Serial.println(" %");

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
