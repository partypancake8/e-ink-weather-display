# Project Plan — ESP32-S3 Wireless Weather Sensor

## Goal

Build a battery-powered, Wi-Fi-connected sensor node that reads temperature and humidity via SHT45, monitors battery state via MAX17048, exposes a live web dashboard, and supports full wireless OTA firmware updates — with no USB access needed after the first flash.

---

## Architecture

```
┌─────────────────────────────────────────────────┐
│  Adafruit Feather ESP32-S3                      │
│                                                 │
│  SHT45 ──I²C──► loop() ──► SensorData struct   │
│  MAX17048 ─I²C──┘            │                 │
│                              ▼                 │
│  WebServer ◄──── handleData() / handleRoot()   │
│       │                      │                 │
│       │                      ▼                 │
│  /update ──► HTTPClient ──► Update class       │
│              (OTA pull)      │                 │
│                              ▼                 │
│                        ESP.restart()           │
└─────────────────────────────────────────────────┘
        │                        ▲
        │ JSON /data (every 2s)  │ firmware.bin (OTA)
        ▼                        │
   Browser dashboard        ./run.sh (Mac)
   (canvas graph, table)    arduino-cli + serve_ota.py
```

### Key Design Decisions

| Decision | Rationale |
|---|---|
| HTTPClient + Update (raw) instead of HTTPUpdate library | HTTPUpdate swallows errors silently — raw gives us the exact error string via `/ota-error` |
| Pull OTA (board fetches from Mac) | Push OTA (ArduinoOTA) requires direct TCP from Mac → board, blocked by AP client isolation on most routers |
| `min_spiffs` partition scheme | Default TinyUF2 partition has no OTA slot — `min_spiffs` gives 1.9 MB app + OTA + 128 KB SPIFFS |
| `serve_ota.py` custom server | Logs exact byte count per transfer; confirms full binary was received before board reboots |
| 2s read interval | Balances sensor responsiveness vs. ESP32 HTTP server latency |
| Rolling 10-sample avg | Smooths SHT45 noise without introducing lag visible to the user |

---

## Development Phases

### Phase 0 — I²C Bus Scan (`validation/phase0_i2c_scan`)
Confirmed SHT45 (0x44) and MAX17048 (0x36) addressable on the default I²C bus.

### Phase 1 — SHT45 Baseline (`validation/phase1_sht45`)
Validated SHT45 reads at high precision mode. Established °C → °F conversion.

### Phase 2 — Battery Gauge (`validation/phase2_battery`)
Confirmed MAX17048 cellPercent() and cellVoltage() are stable. Defined LOW (20%) and CRITICAL (10%) thresholds.

### Phase 3 — Wi-Fi (`validation/phase3_wifi`)
Brought up WIFI_STA mode, confirmed DHCP assignment, built first `/data` JSON endpoint.

### Phase 4 — Combined Sensor + Wi-Fi (`validation/phase4_final`)
Merged phases 1–3. Confirmed sensors readable while HTTP server handles concurrent requests.

### Phase 5 — NeoPixel LED State Machine (`validation/phase5_status_leds`)
Implemented non-blocking LED state machine (breathe, heartbeat, blink patterns) covering all operational states. Validated no `delay()` calls block the HTTP server.

### Phase 6 — Production Firmware (`firmware/phase6_http_server`) ✅ current
Full feature set:
- SHT45 + MAX17048 + NeoPixel LED state machine
- `/` dashboard with live canvas temperature graph
- `/data` JSON with `tempFAvg` rolling average
- `/health` liveness endpoint
- `/reboot` escape hatch
- `/ota-error` last OTA result readable over HTTP
- `/update?url=` HTTP OTA (HTTPClient + Update)
- 2s read interval, 10-sample rolling average

### Phase 7 — OTA Diagnostic (`validation/phase7_ota_test`)
Isolated HTTP OTA test sketch used to prove the OTA mechanism worked before integrating into Phase 6. Kept for reference.

---

## Deployment Workflow

> **Run `./run.sh` yourself every time. Do not automate or delegate this step.**

```
./run.sh
  ├─ arduino-cli compile (FQBN with min_spiffs)
  ├─ kill any old serve_ota.py on port 8080
  ├─ python3 /tmp/serve_ota.py firmware.bin 8080  (background)
  ├─ GET http://10.0.0.151/update?url=http://10.0.0.162:8080/firmware.bin
  └─ poll /health every 2s (up to 90s) → report /data on success
```

The board's NeoPixel shows blue → bright blue during download, then 5× purple flashes before rebooting onto the new firmware.

---

## Known Constraints

- **Board IP**: DHCP-assigned (`10.0.0.151`). If it changes, update `BOARD_IP` in `run.sh`.
- **Mac IP**: Must match `MAC_IP` in `run.sh` so the board can reach the HTTP server.
- **AP client isolation**: Routers with client isolation will break OTA (board can't reach Mac). The FBI Van network does not have this issue.
- **Partition scheme**: Must be `min_spiffs`. Any other scheme (especially TinyUF2 NoOTA default) will fail with "Partition Could Not be Found" at OTA time. You only need to re-USB-flash if switching partition schemes.
- **serve_ota.py location**: The script lives at `/tmp/serve_ota.py`. If `/tmp` is cleared, `run.sh` will fail at step 2. Keep a copy in the repo or recreate from the custom byte-counting implementation.

---

## Future Ideas

- [ ] Add e-ink display output (Feather Wing or SPI panel)
- [ ] Persist sensor history to SPIFFS and serve a multi-hour graph
- [ ] Add humidity to the live graph (second trace)
- [ ] mDNS hostname so URL is `http://weathersensor.local/` instead of IP
- [ ] Deep sleep between reads to extend battery life
- [ ] MQTT publish for Home Assistant integration
- [ ] Add outdoor vs. indoor delta when second sensor is available
