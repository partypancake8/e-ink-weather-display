/*
 * ============================================================
 * Phase 3 — SHT45 + Battery + Wi-Fi Connection Test
 * Adafruit ESP32-S3 Feather
 * ============================================================
 *
 * Libraries (install via Sketch > Include Library > Manage Libraries):
 *   1. "Adafruit SHT4x Library"    by Adafruit  (search: SHT4x)
 *   2. "Adafruit MAX1704X"          by Adafruit  (search: MAX1704X)
 *   3. WiFi.h — bundled with the ESP32 Arduino core; no separate install
 *
 * Fill in your network details in the credentials section below.
 *
 * Expected output on successful connect:
 *   [WI-FI]   Connecting to "MyNetwork" ...........
 *   [WI-FI]   Connected.   IP   : 192.168.1.42
 *   [WI-FI]               RSSI  : -58 dBm
 *
 * Then every 5 s:
 *   === Reading #1 ===
 *   Temperature  : 72.5 F
 *   Humidity     : 45.2 %RH
 *   Batt Voltage : 4.187 V
 *   Batt SOC     : 98.3 %
 *   Wi-Fi        : Connected  |  IP: 192.168.1.42  |  RSSI: -58 dBm
 *
 * Arduino IDE settings:
 *   Board           : Adafruit Feather ESP32-S3 No PSRAM
 *   USB CDC On Boot : Enabled
 * ============================================================
 */

#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_MAX1704X.h>

// ================================================================
// Wi-Fi credentials — edit these
// ================================================================
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";
// ================================================================

Adafruit_SHT4x    sht4;
Adafruit_MAX17048 maxlipo;

// ---- forward declarations ----
void connectWiFi();
void printReadings();

// ================================================================

void setup() {
  Serial.begin(115200);
  unsigned long t = millis();
  while (!Serial && (millis() - t) < 3000) delay(10);

  Wire.begin();

  Serial.println();
  Serial.println("=========================================");
  Serial.println("  SHT45 + Battery + Wi-Fi  |  Phase 3");
  Serial.println("=========================================");

  // --- SHT45 ---
  Serial.print("[SHT45]   ");
  if (!sht4.begin()) {
    Serial.println("NOT FOUND. Halting.");
    while (1) delay(10);
  }
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);
  Serial.println("OK");

  // --- MAX17048 ---
  Serial.print("[BATTERY] ");
  if (!maxlipo.begin()) {
    Serial.println("NOT FOUND. Halting.");
    while (1) delay(10);
  }
  Serial.print("OK  (ID: 0x");
  Serial.print(maxlipo.getChipID(), HEX);
  Serial.println(")");

  // --- Wi-Fi ---
  connectWiFi();

  Serial.println("-----------------------------------------");
  Serial.println("Reading every 5 seconds...");
  Serial.println();
}

// ================================================================

void loop() {
  printReadings();
  delay(5000);
}

// ================================================================

void connectWiFi() {
  Serial.print("[WI-FI]   Connecting to \"");
  Serial.print(WIFI_SSID);
  Serial.print("\" ");

  WiFi.mode(WIFI_STA);  // Station mode — connect to AP, don't create one
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // 40 attempts × 500 ms = 20 s timeout
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
    Serial.print("[WI-FI]               RSSI  : ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("[WI-FI]   Connection FAILED after 20 s.");
    Serial.println("          -> Check WIFI_SSID / WIFI_PASSWORD.");
    Serial.println("          -> Is the Mac sharing its Wi-Fi? 2.4 GHz only for ESP32.");
    Serial.println("          -> Continuing without Wi-Fi.");
    // Do NOT halt — sensors still work without Wi-Fi.
  }
}

// ================================================================

void printReadings() {
  static uint32_t num = 0;
  num++;

  sensors_event_t humid_ev, temp_ev;
  sht4.getEvent(&humid_ev, &temp_ev);

  float tempF   = (temp_ev.temperature * 9.0f / 5.0f) + 32.0f;
  float rh      = humid_ev.relative_humidity;
  float battV   = maxlipo.cellVoltage();
  float battPct = constrain(maxlipo.cellPercent(), 0.0f, 100.0f);

  Serial.print("=== Reading #");
  Serial.print(num);
  Serial.println(" ===");

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

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("  Wi-Fi        : Connected  |  IP: ");
    Serial.print(WiFi.localIP());
    Serial.print("  |  RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("  Wi-Fi        : Not connected");
  }

  Serial.println();
}
