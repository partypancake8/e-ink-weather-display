# ESP32-S3 Wireless Weather Sensor

Wi-Fi-connected temperature, humidity, and battery monitor running on an Adafruit Feather ESP32-S3. Serves a live dashboard with auto-updating sensor data and a real-time temperature graph, and supports full wireless OTA firmware updates with no USB required after initial setup.

---

## Hardware

| Component | Part |
|---|---|
| MCU | Adafruit Feather ESP32-S3 2MB PSRAM |
| Temp / Humidity | Adafruit SHT45 (I²C) |
| Battery gauge | MAX17048 (I²C — built into Feather) |
| Status LED | NeoPixel (built into Feather, GPIO 33) |
| Power | 3.7V LiPo via JST |

---

## Project Structure

```
e-ink-weather-display/
├── firmware/
│   └── phase6_http_server/     ← production firmware (always flash this)
│       └── phase6_http_server.ino
├── validation/                 ← incremental validation sketches (reference only)
│   ├── phase0_i2c_scan/        I²C bus scan
│   ├── phase1_sht45/           SHT45 temp + humidity baseline
│   ├── phase2_battery/         MAX17048 fuel gauge
│   ├── phase3_wifi/            Wi-Fi connect + JSON endpoint
│   ├── phase4_final/           Combined sensor + Wi-Fi
│   ├── phase5_status_leds/     NeoPixel LED state machine
│   └── phase7_ota_test/        Standalone HTTP OTA diagnostic
├── run.sh                      ← the ONE command to build + deploy
├── plan.md                     ← architecture notes and development log
└── README.md
```

---

## Setup

### Arduino CLI

```bash
# Install ESP32 core
arduino-cli core install esp32:esp32

# Install required libraries
arduino-cli lib install "Adafruit SHT4x Library"
arduino-cli lib install "Adafruit MAX1704X"
arduino-cli lib install "Adafruit NeoPixel"
arduino-cli lib install "Adafruit BusIO"
```

### First Flash (USB — one time only)

Connect via USB and flash with the `min_spiffs` partition scheme:

```bash
arduino-cli upload \
  --fqbn "esp32:esp32:adafruit_feather_esp32s3:CDCOnBoot=cdc,PartitionScheme=min_spiffs" \
  -p /dev/cu.usbmodem2101 \
  firmware/phase6_http_server
```

> **Why `min_spiffs`?** The default `TinyUF2 4MB No OTA` partition scheme has no OTA partition.
> You must flash with `min_spiffs` (1.9 MB app + OTA) at least once via USB.
> After that, all updates are wireless via `./run.sh`.

### Edit Wi-Fi Credentials

Open `firmware/phase6_http_server/phase6_http_server.ino` and update:

```cpp
const char* WIFI_SSID     = "your_network";
const char* WIFI_PASSWORD = "your_password";
```

---

## Deploying Firmware

> **Always run this yourself. Never delegate to an AI assistant or automation.**

```bash
./run.sh
```

This single command:
1. Compiles the firmware with `arduino-cli`
2. Starts a local HTTP server to serve the binary
3. Triggers OTA download on the board via `GET /update?url=...`
4. Polls `/health` every 2s until the board comes back online
5. Reports the final `/data` JSON to confirm success

The board's NeoPixel turns solid blue during download, then flashes **purple 5×** on success before rebooting.

### Flash a different sketch

```bash
./run.sh firmware/my_other_sketch
```

---

## Dashboard

Open `http://10.0.0.151/` in any browser on the same network.

| Field | Description |
|---|---|
| Temperature | Latest SHT45 reading (°F) |
| Avg temp (10 rdgs) | Rolling 10-sample average |
| Humidity | Relative humidity (%RH) |
| Battery voltage | Raw cell voltage (V) |
| Battery SOC | State of charge (%) with LOW / CRITICAL warnings |
| Wi-Fi | Connection status + RSSI |
| Uptime | Time since last boot |
| Reading # | Total sensor readings since boot |
| Graph | Live temperature chart — last 60 readings (~2 min) |

The page polls `/data` every 2 seconds with no full-page reload.

---

## HTTP Endpoints

| Endpoint | Description |
|---|---|
| `GET /` | Live HTML dashboard |
| `GET /data` | JSON sensor blob |
| `GET /health` | `200 OK` or `503 DEGRADED` (for uptime monitoring) |
| `GET /reboot` | Force reboot the board |
| `GET /ota-error` | Last OTA result string |
| `GET /update?url=` | Trigger OTA from a URL |

---

## LED Status

| Color / Pattern | State |
|---|---|
| White breathe | Booting |
| Yellow blink | Wi-Fi connecting |
| Double green flash | Wi-Fi connected |
| Dim green heartbeat | Healthy (every ~1s) |
| Brief blue flash | Sensor read (every 2s) |
| Blue solid → bright | OTA downloading |
| Purple × 5 → reboot | OTA success |
| Orange blink | Wi-Fi lost |
| Purple breathe | Battery low |
| Red fast blink | Battery critical |

---

## Board Details

- **FQBN:** `esp32:esp32:adafruit_feather_esp32s3:CDCOnBoot=cdc,PartitionScheme=min_spiffs`
- **Board IP:** `10.0.0.151` (DHCP — check Serial Monitor if it changes)
- **Read interval:** 2000 ms
- **OTA mechanism:** HTTPClient + Update (raw), pull model
