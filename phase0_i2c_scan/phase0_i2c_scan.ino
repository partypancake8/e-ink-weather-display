/*
 * ============================================================
 * Phase 0 — I2C Bus Scanner
 * Adafruit ESP32-S3 Feather
 * ============================================================
 *
 * Purpose:
 *   Confirm all expected I2C devices are visible before writing
 *   any device-specific code. Run this first every time you
 *   change wiring.
 *
 * Expected addresses with this hardware:
 *   0x36  MAX17048 fuel gauge   (soldered on Feather PCB)
 *   0x44  SHT45               (STEMMA QT, default address)
 *
 * Arduino IDE settings:
 *   Board           : Adafruit Feather ESP32-S3 No PSRAM
 *                     (or "...ESP32-S3 4MB Flash 2MB PSRAM" if yours has PSRAM)
 *   USB CDC On Boot : Enabled   <-- REQUIRED for Serial over USB-C
 *   Port            : the COMx / /dev/cu.usbmodem... that appears after boot
 * ============================================================
 */

#include <Wire.h>

void setup() {
  Serial.begin(115200);

  // Wait up to 3 s for USB-CDC to enumerate.
  // Using a timeout so the sketch still boots cleanly on battery with no PC.
  unsigned long t = millis();
  while (!Serial && (millis() - t) < 3000) delay(10);

  Wire.begin();
  delay(100); // let bus settle after power-on

  Serial.println();
  Serial.println("================================");
  Serial.println("  I2C Scanner  |  Phase 0");
  Serial.println("================================");
  Serial.println("Scanning addresses 0x01 - 0x7E...");
  Serial.println();

  uint8_t found = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t err = Wire.endTransmission();

    if (err == 0) {
      Serial.print("  [FOUND] 0x");
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);

      switch (addr) {
        case 0x36: Serial.print("  ->  MAX17048 (LiPo fuel gauge)");       break;
        case 0x44: Serial.print("  ->  SHT45 (temp/humidity, default)");   break;
        case 0x45: Serial.print("  ->  SHT45 (temp/humidity, alt addr)");  break;
        case 0x60: Serial.print("  ->  ATECC608 (crypto, on some Feathers)"); break;
        default:   break;
      }
      Serial.println();
      found++;
    }
  }

  Serial.println();

  if (found == 0) {
    Serial.println("  [ERROR] No I2C devices found.");
    Serial.println("  Checks:");
    Serial.println("    -> STEMMA QT cable fully seated at both ends?");
    Serial.println("    -> Board powered (3.3 V on STEMMA QT pin 1)?");
    Serial.println("    -> USB CDC On Boot set to Enabled in Tools menu?");
  } else {
    Serial.print("  Total found: ");
    Serial.println(found);

    bool hasFuelGauge = false, hasSHT = false;
    // Re-scan just to check for known addresses (simple approach)
    Wire.beginTransmission(0x36);
    hasFuelGauge = (Wire.endTransmission() == 0);
    Wire.beginTransmission(0x44);
    hasSHT = (Wire.endTransmission() == 0);
    if (!hasSHT) {
      Wire.beginTransmission(0x45);
      hasSHT = (Wire.endTransmission() == 0);
    }

    if (hasFuelGauge && hasSHT) {
      Serial.println("  [PASS] MAX17048 and SHT45 both visible. Ready for Phase 1.");
    } else {
      if (!hasFuelGauge) Serial.println("  [WARN] MAX17048 (0x36) missing. Is a LiPo connected?");
      if (!hasSHT)       Serial.println("  [WARN] SHT45 (0x44/0x45) missing. Check STEMMA QT.");
    }
  }

  Serial.println("================================");
  Serial.println("Scan complete.");
}

void loop() {
  // Single-shot scan — nothing in loop.
}
