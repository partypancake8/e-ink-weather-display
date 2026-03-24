/*
 * ============================================================
 * Phase 2 — SHT45 + LiPo Battery Monitor (MAX17048)
 * Adafruit ESP32-S3 Feather
 * ============================================================
 *
 * Libraries (install via Sketch > Include Library > Manage Libraries):
 *   1. "Adafruit SHT4x Library"    by Adafruit  (search: SHT4x)
 *   2. "Adafruit MAX1704X"          by Adafruit  (search: MAX1704X)
 *      -> installs Adafruit BusIO as a dependency
 *
 * ---- Board-specific battery caveat (ESP32-S3 Feather) ----
 *
 *   This board uses a MAX17048 fuel-gauge IC at I2C address 0x36.
 *   It uses a coulomb-counting + OCV cell model — NOT a voltage
 *   divider. Do NOT use analogRead() for battery voltage here.
 *   Older Adafruit Feather ESP32 (V2) boards use pin A13/35 for
 *   battery voltage; that technique does not apply to ESP32-S3.
 *
 *   When USB is actively charging the LiPo:
 *     - cellVoltage() will read ~4.1–4.2 V (not the resting ~3.7 V)
 *     - cellPercent() remains valid; the MAX17048 algorithm accounts
 *       for charge current in its SOC estimation
 *
 *   On first power-on after long storage:
 *     - The MAX17048's cell model has not yet run; SOC% may be
 *       off by 5–15% until it completes 1–2 full charge/discharge
 *       cycles. This is normal behaviour, not a bug.
 *
 *   If no LiPo is connected:
 *     - maxlipo.begin() will return false (I2C device missing)
 *     - The MAX17048 needs the VBAT pin connected to function
 *
 * Expected output (every 2 s), with USB charging and full battery:
 *   --- Reading ---
 *   Temperature  : 72.5 F
 *   Humidity     : 45.2 %RH
 *   Batt Voltage : 4.187 V
 *   Batt SOC     : 98.3 %
 *
 * Arduino IDE settings:
 *   Board           : Adafruit Feather ESP32-S3 No PSRAM
 *   USB CDC On Boot : Enabled
 * ============================================================
 */

#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_MAX1704X.h>

Adafruit_SHT4x    sht4;
Adafruit_MAX17048 maxlipo;

void setup() {
  Serial.begin(115200);
  unsigned long t = millis();
  while (!Serial && (millis() - t) < 3000) delay(10);

  Wire.begin();

  Serial.println();
  Serial.println("===================================");
  Serial.println("  SHT45 + Battery Test  |  Phase 2");
  Serial.println("===================================");

  // --- SHT45 ---
  Serial.print("[SHT45]   ");
  if (!sht4.begin()) {
    Serial.println("NOT FOUND. Halting.");
    Serial.println("          -> Run Phase 0 scan; check STEMMA QT.");
    while (1) delay(10);
  }
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);
  Serial.println("OK");

  // --- MAX17048 ---
  Serial.print("[BATTERY] ");
  if (!maxlipo.begin()) {
    Serial.println("MAX17048 NOT FOUND. Halting.");
    Serial.println("          -> Is a LiPo connected to the JST-PH port?");
    Serial.println("          -> Run Phase 0 scan; expect 0x36.");
    while (1) delay(10);
  }
  // getChipID() reads the IC_VERSION register (0x08).
  // For MAX17048, this is typically 0x0010 or similar.
  Serial.print("OK  (MAX17048 chip ID: 0x");
  Serial.print(maxlipo.getChipID(), HEX);
  Serial.println(")");

  Serial.println("-----------------------------------");
  Serial.println("Reading every 2 seconds...");
  Serial.println();
}

void loop() {
  sensors_event_t humid_ev, temp_ev;
  sht4.getEvent(&humid_ev, &temp_ev);

  float tempF   = (temp_ev.temperature * 9.0f / 5.0f) + 32.0f;
  float rh      = humid_ev.relative_humidity;
  float battV   = maxlipo.cellVoltage();
  // cellPercent() may briefly exceed 100% on a freshly charged cell.
  // constrain() keeps display values logical.
  float battPct = constrain(maxlipo.cellPercent(), 0.0f, 100.0f);

  Serial.println("--- Reading ---");
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

  Serial.println();
  delay(2000);
}
