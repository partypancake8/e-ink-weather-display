/*
 * ============================================================
 * Phase 1 — SHT45 Temperature & Humidity
 * Adafruit ESP32-S3 Feather
 * ============================================================
 *
 * Libraries (install via Sketch > Include Library > Manage Libraries):
 *   1. "Adafruit SHT4x Library"    by Adafruit  (search: SHT4x)
 *      -> Adafruit BusIO and Adafruit Unified Sensor install automatically
 *         as dependencies. Accept "Install all" when prompted.
 *
 * Arduino IDE settings:
 *   Board           : Adafruit Feather ESP32-S3 No PSRAM
 *   USB CDC On Boot : Enabled
 *
 * Expected serial output (every 2 s):
 *   --- SHT45 ---
 *   Temperature : 72.5 F
 *   Humidity    : 45.2 %RH
 *
 * Normal ranges at room temperature:
 *   Temperature : ~65–80 F  (if you breathe on it, it ticks up)
 *   Humidity    : ~30–60 %RH  (typical indoors)
 *
 * If temperature reads implausibly high (>120 F) and does not
 * change: sensor is reading its own self-heating. Confirm the
 * heater is OFF (SHT4X_NO_HEATER) and that you are not
 * blocking airflow around the sensor.
 * ============================================================
 */

#include <Wire.h>
#include <Adafruit_SHT4x.h>

Adafruit_SHT4x sht4;

void setup() {
  Serial.begin(115200);
  unsigned long t = millis();
  while (!Serial && (millis() - t) < 3000) delay(10);

  Wire.begin();

  Serial.println();
  Serial.println("================================");
  Serial.println("  SHT45 Sensor Test  |  Phase 1");
  Serial.println("================================");

  if (!sht4.begin()) {
    Serial.println("[ERROR] SHT45 not found at 0x44.");
    Serial.println("        -> Run Phase 0 I2C scan to diagnose.");
    Serial.println("        -> Check STEMMA QT cable; try reseating.");
    while (1) delay(10); // halt — nothing useful to do
  }

  // SHT4X_HIGH_PRECISION   : 8.3 ms measurement, ±0.1 °C, ±0.5 %RH
  // SHT4X_MED_PRECISION    : 4.5 ms, ±0.3 °C
  // SHT4X_LOW_PRECISION    : 1.7 ms, ±0.8 °C
  sht4.setPrecision(SHT4X_HIGH_PRECISION);

  // Keep heater OFF for ambient readings.
  // Enable only to burn off condensation; it skews temperature readings.
  sht4.setHeater(SHT4X_NO_HEATER);

  Serial.println("[OK] SHT45 initialized.");
  Serial.println("     Precision : HIGH");
  Serial.println("     Heater    : OFF");
  Serial.println("--------------------------------");
  Serial.println("Reading every 2 seconds...");
  Serial.println();
}

void loop() {
  sensors_event_t humid_ev, temp_ev;
  // getEvent() blocks for the measurement duration (~8 ms at HIGH precision)
  sht4.getEvent(&humid_ev, &temp_ev);

  float tempC = temp_ev.temperature;
  float tempF = (tempC * 9.0f / 5.0f) + 32.0f;
  float rh    = humid_ev.relative_humidity;

  Serial.println("--- SHT45 ---");
  Serial.print("  Temperature : ");
  Serial.print(tempF, 1);
  Serial.println(" F");
  Serial.print("  Humidity    : ");
  Serial.print(rh, 1);
  Serial.println(" %RH");
  Serial.println();

  delay(2000);
}
