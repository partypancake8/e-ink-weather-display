/*
 * ============================================================
 * Phase 6 — Wi-Fi HTTP Server + OTA Updates
 * Adafruit ESP32-S3 Feather  +  SHT45  +  LiPo  +  NeoPixel
 * ============================================================
 *
 * Endpoints (replace IP with whatever Serial Monitor prints):
 *
 *   GET /          — human-readable plain-text summary
 *   GET /data      — JSON  { tempF, humidity, battV, battPct,
 *                            rssi, uptime, reading }
 *   GET /health    — "OK" (200) or "DEGRADED" (503) for monitoring
 *
 * Try from your Mac terminal:
 *   curl http://10.0.0.151/
 *   curl http://10.0.0.151/data
 *   watch -n 5 curl -s http://10.0.0.151/data
 *
 * Or open http://10.0.0.151/ in a browser.
 *
 * OTA flashing (no USB needed after first flash):
 *   Arduino IDE: Tools > Port > "esp32-feather" (network port, appears after boot)
 *   arduino-cli:  arduino-cli upload --fqbn ... -p <IP_ADDR> --protocol network <sketch>
 *   Password:     set OTA_PASSWORD below (leave blank to disable auth)
 *
 * Libraries — all bundled with the ESP32 core, none extra:
 *   WebServer.h, WiFi.h, ArduinoOTA.h
 * Plus the usual:
 *   Adafruit SHT4x Library, Adafruit MAX1704X, Adafruit NeoPixel
 *
 * Arduino IDE settings:
 *   Board           : Adafruit Feather ESP32-S3 2MB PSRAM
 *   USB CDC On Boot : Enabled
 * ============================================================
 */

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Adafruit_NeoPixel.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_MAX1704X.h>

// ================================================================
// Wi-Fi credentials
// ================================================================
const char* WIFI_SSID     = "FBI Van";
const char* WIFI_PASSWORD = "sambearrosie";
// ================================================================

static const uint32_t READ_INTERVAL_MS = 2000;
static const uint8_t  TEMP_AVG_SAMPLES  = 10;
static const float    BATT_LOW_PCT     = 20.0f;
static const float    BATT_CRIT_PCT    = 10.0f;
static const uint16_t HTTP_PORT        = 80;

// ---- Peripherals ----
Adafruit_NeoPixel pixel(NEOPIXEL_NUM, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
Adafruit_SHT4x    sht4;
Adafruit_MAX17048 maxlipo;
WebServer         server(HTTP_PORT);

// ---- Shared sensor state (updated by loop, read by HTTP handlers) ----
struct SensorData {
  float    tempF    = 0;
  float    tempFAvg = 0;   // rolling average over last TEMP_AVG_SAMPLES readings
  float    humidity = 0;
  float    battV    = 0;
  float    battPct  = 0;
  int32_t  rssi     = 0;
  uint32_t uptime   = 0;   // seconds
  uint32_t reading  = 0;
  bool     wifiUp   = false;
};
static SensorData latest;
static volatile bool otaInProgress = false;  // set during HTTP OTA to pause sensors
static          uint8_t otaBrightness = 20;    // written by OTA progress callback
static          String  otaLastError  = "none"; // last OTA error, readable via /ota-error

// ================================================================
// LED (same state machine as Phase 5)
// ================================================================
enum LedState {
  LED_BOOTING, LED_WIFI_CONNECTING, LED_WIFI_CONNECTED,
  LED_HEALTHY, LED_WIFI_LOST, LED_BATT_LOW, LED_BATT_CRITICAL,
  LED_OTA_PROGRESS  // blue breathe during OTA flash
};
static LedState  ledState    = LED_BOOTING;
static uint32_t  ledTimer    = 0;
static bool      ledPhase    = false;
static uint8_t   breatheVal  = 0;
static int8_t    breatheDir  = 1;
static bool      flashPending = false;
static uint32_t  flashColor   = 0;
static uint32_t  flashEnd     = 0;

static inline uint32_t col(uint8_t r, uint8_t g, uint8_t b) {
  return pixel.Color(r, g, b);
}
void setLedState(LedState s) { ledState = s; ledTimer = millis(); ledPhase = false; breatheVal = 0; breatheDir = 1; }

void flashReadIndicator() {
  flashColor   = col(0, 80, 220);   // blue = data capture
  flashEnd     = millis() + 120;
  flashPending = true;
}

void updateLed() {
  uint32_t now = millis();
  if (flashPending) {
    if (now < flashEnd) { pixel.setPixelColor(0, flashColor); pixel.show(); return; }
    flashPending = false;
  }
  switch (ledState) {
    case LED_BOOTING: {
      if (now - ledTimer < 16) break; ledTimer = now;
      breatheVal += breatheDir * 3;
      if (breatheVal >= 120) { breatheVal = 120; breatheDir = -1; }
      if (breatheVal == 0)   { breatheDir  =  1; }
      pixel.setPixelColor(0, col(breatheVal, breatheVal, breatheVal)); pixel.show(); break;
    }
    case LED_WIFI_CONNECTING: {
      if (now - ledTimer < 200) break; ledTimer = now;
      ledPhase = !ledPhase;
      pixel.setPixelColor(0, ledPhase ? col(180, 120, 0) : col(0, 0, 0)); pixel.show(); break;
    }
    case LED_WIFI_CONNECTED: {
      // Double green flash: breatheVal tracks sub-step (0-3), transition on 4
      // Steps: on(150) off(120) on(150) off(120) → HEALTHY
      static const uint16_t delays[] = {150, 120, 150, 120};
      if (now - ledTimer >= delays[breatheVal]) {
        ledTimer = now;
        breatheVal++;
        if (breatheVal >= 4) { breatheVal = 0; setLedState(LED_HEALTHY); break; }
      }
      bool lit = (breatheVal % 2 == 0);
      pixel.setPixelColor(0, lit ? col(0, 200, 0) : col(0, 0, 0)); pixel.show();
      break;
    }
    case LED_HEALTHY: {
      if (!ledPhase && now - ledTimer >= 920) {
        ledTimer = now; ledPhase = true;
        pixel.setPixelColor(0, col(0, 40, 0)); pixel.show();
      } else if (ledPhase && now - ledTimer >= 80) {
        ledTimer = now; ledPhase = false;
        pixel.clear(); pixel.show();
      }
      break;
    }
    case LED_WIFI_LOST: {
      if (now - ledTimer < 600) break; ledTimer = now;
      ledPhase = !ledPhase;
      pixel.setPixelColor(0, ledPhase ? col(200, 60, 0) : col(0, 0, 0)); pixel.show(); break;
    }
    case LED_BATT_LOW: {
      if (now - ledTimer < 20) break; ledTimer = now;
      breatheVal += breatheDir * 2;
      if (breatheVal >= 80) { breatheVal = 80; breatheDir = -1; }
      if (breatheVal == 0)  { breatheDir  =  1; }
      pixel.setPixelColor(0, col(breatheVal / 2, 0, breatheVal)); pixel.show(); break;
    }
    case LED_BATT_CRITICAL: {
      if (now - ledTimer < 150) break; ledTimer = now;
      ledPhase = !ledPhase;
      pixel.setPixelColor(0, ledPhase ? col(220, 0, 0) : col(0, 0, 0)); pixel.show(); break;
    }
    case LED_OTA_PROGRESS: {
      pixel.setPixelColor(0, col(0, 0, otaBrightness)); pixel.show(); break;
    }
  }
}

// ================================================================
// HTTP route handlers
// ================================================================

// GET /  — auto-updating HTML dashboard (polls /data every READ_INTERVAL_MS via JS fetch)
void handleRoot() {
  // Page fetches /data on load and every READ_INTERVAL_MS ms — no hard reload.
  String html = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-S3 Feather</title>
<style>
  body{font-family:monospace;background:#111;color:#e0e0e0;display:flex;
       justify-content:center;align-items:flex-start;min-height:100vh;margin:0;padding:2rem;}
  .card{background:#1e1e1e;border-radius:10px;padding:1.5rem 2rem;min-width:300px;
        box-shadow:0 4px 20px rgba(0,0,0,.5);}
  h2{margin:0 0 1rem;color:#7ec8e3;font-size:1.1rem;letter-spacing:.05em;}
  table{border-collapse:collapse;width:100%;}
  td{padding:.35rem .5rem;font-size:.95rem;}
  td:first-child{color:#888;padding-right:1.5rem;white-space:nowrap;}
  td:last-child{color:#fff;font-weight:bold;}
  .ok{color:#4caf50!important;} .warn{color:#ff9800!important;} .crit{color:#f44336!important;}
  #status{font-size:.75rem;color:#555;margin-top:1rem;text-align:right;}
  .dot{display:inline-block;width:8px;height:8px;border-radius:50%;
       background:#4caf50;margin-right:.4rem;animation:pulse 2s infinite;}
  @keyframes pulse{0%,100%{opacity:1;}50%{opacity:.3;}}
  .graph-wrap{margin-top:1.2rem;}
  canvas{width:100%;height:auto;display:block;border-radius:6px;background:#161616;}
</style>
</head>
<body>
<div class="card">
  <h2><span class="dot"></span>ESP32-S3 Feather — Live Sensor Data</h2>
  <table>
    <tr><td>Temperature</td>  <td id="tempF">—</td></tr>
    <tr><td>Avg temp (10 rdgs)</td><td id="tempAvg">—</td></tr>
    <tr><td>Humidity</td>     <td id="humid">—</td></tr>
    <tr><td>Battery voltage</td><td id="battV">—</td></tr>
    <tr><td>Battery SOC</td>  <td id="battP">—</td></tr>
    <tr><td>Wi-Fi</td>        <td id="wifi">—</td></tr>
    <tr><td>RSSI</td>         <td id="rssi">—</td></tr>
    <tr><td>Uptime</td>       <td id="uptime">—</td></tr>
    <tr><td>Reading #</td>    <td id="reading">—</td></tr>
  </table>
  <div class="graph-wrap">
    <canvas id="tempGraph" width="420" height="150"></canvas>
  </div>
  <div id="status">fetching...</div>
</div>
<script>
  const INTERVAL = )rawhtml";
  html += String(READ_INTERVAL_MS);
  html += R"rawhtml(;
  const MAX_PTS = 60;
  let history = [];
  function drawGraph(){
    const c=document.getElementById('tempGraph');
    if(!c)return;
    const ctx=c.getContext('2d');
    const W=c.width,H=c.height;
    const PL=42,PR=10,PT=10,PB=26;
    const pw=W-PL-PR,ph=H-PT-PB;
    ctx.clearRect(0,0,W,H);
    if(history.length<2){
      ctx.fillStyle='#555';ctx.font='11px monospace';ctx.textAlign='center';
      ctx.fillText('Collecting data...',W/2,H/2);return;
    }
    const mn=Math.min(...history)-0.3,mx=Math.max(...history)+0.3,rng=mx-mn||1;
    ctx.strokeStyle='#2a2a2a';ctx.lineWidth=1;
    for(let i=0;i<=4;i++){
      const y=PT+ph*(1-i/4);
      ctx.beginPath();ctx.moveTo(PL,y);ctx.lineTo(PL+pw,y);ctx.stroke();
      ctx.fillStyle='#555';ctx.font='10px monospace';ctx.textAlign='right';
      ctx.fillText((mn+rng*i/4).toFixed(1)+'\u00b0',PL-3,y+3.5);
    }
    ctx.strokeStyle='#7ec8e3';ctx.lineWidth=2;ctx.lineJoin='round';
    ctx.beginPath();
    const n=Math.max(history.length,2)-1;
    history.forEach((t,i)=>{
      const x=PL+pw*i/n;
      const y=PT+ph*(1-(t-mn)/rng);
      i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
    });
    ctx.stroke();
    const lx=PL+pw*(history.length-1)/n;
    const ly=PT+ph*(1-(history[history.length-1]-mn)/rng);
    ctx.fillStyle='#7ec8e3';ctx.beginPath();ctx.arc(lx,ly,3.5,0,2*Math.PI);ctx.fill();
    ctx.fillStyle='#444';ctx.font='10px monospace';ctx.textAlign='center';
    ctx.fillText('\u2190 last '+Math.round(history.length*INTERVAL/1000)+'s',PL+pw/2,H-6);
  }
  function cls(pct){
    if(pct < 10) return 'crit';
    if(pct < 20) return 'warn';
    return 'ok';
  }
  function set(id, text, className){
    const el = document.getElementById(id);
    el.textContent = text;
    if(className){ el.className = className; }
  }
  function update(){
    fetch('/data')
      .then(r => r.json())
      .then(d => {
        set('tempF',   d.tempF.toFixed(1)    + ' °F');
        set('tempAvg', d.tempFAvg.toFixed(1)  + ' °F');
        set('humid',   d.humidity.toFixed(1) + ' %RH');
        set('battV',   d.battV.toFixed(3)    + ' V');
        const pct = d.battPct.toFixed(1);
        set('battP',   pct + ' %' + (d.battPct < 10 ? '  ⚠ CRITICAL' : d.battPct < 20 ? '  ⚠ LOW' : ''), cls(d.battPct));
        set('wifi',    d.wifiUp ? 'Connected' : 'Disconnected', d.wifiUp ? 'ok' : 'crit');
        set('rssi',    d.wifiUp ? d.rssi + ' dBm' : '—');
        const h = Math.floor(d.uptime/3600), m = Math.floor((d.uptime%3600)/60), s = d.uptime%60;
        set('uptime',  (h?h+'h ':'') + (m?m+'m ':'') + s+'s');
        set('reading', '#' + d.reading);
        history.push(d.tempF);
        if(history.length > MAX_PTS) history.shift();
        drawGraph();
        document.getElementById('status').textContent =
          'Last updated: ' + new Date().toLocaleTimeString();
      })
      .catch(() => {
        document.getElementById('status').textContent = 'Fetch failed — retrying...';
      });
  }
  update();
  setInterval(update, INTERVAL);
</script>
</body>
</html>
)rawhtml";
  // HTML never changes — tell browsers/phones to cache it for 1 hour.
  // Only /data is fetched repeatedly, and that is tiny (< 200 bytes).
  server.sendHeader("Cache-Control", "public, max-age=3600");
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", html);
}

// GET /data  — JSON
void handleData() {
  String json;
  json.reserve(300);
  json += "{\n";
  json += "  \"tempF\":    "; json += String(latest.tempF,    2); json += ",\n";
  json += "  \"tempFAvg\": "; json += String(latest.tempFAvg, 2); json += ",\n";
  json += "  \"humidity\": "; json += String(latest.humidity, 2); json += ",\n";
  json += "  \"battV\":    "; json += String(latest.battV,    3); json += ",\n";
  json += "  \"battPct\":  "; json += String(latest.battPct,  1); json += ",\n";
  json += "  \"wifiUp\":   "; json += (latest.wifiUp ? "true" : "false");   json += ",\n";
  json += "  \"rssi\":     "; json += String(latest.rssi);      json += ",\n";
  json += "  \"uptime\":   "; json += String(latest.uptime);    json += ",\n";
  json += "  \"reading\":  "; json += String(latest.reading);   json += "\n";
  json += "}\n";
  server.sendHeader("Cache-Control", "no-store");          // always fresh
  server.sendHeader("Connection", "close");                // don't stall keep-alive
  server.sendHeader("Access-Control-Allow-Origin", "*");   // allow phone browsers
  server.send(200, "application/json", json);
}

// GET /health  — simple liveness check
void handleHealth() {
  bool ok = latest.wifiUp && (latest.battPct >= BATT_CRIT_PCT);
  if (ok) {
    server.send(200, "text/plain", "OK\n");
  } else {
    String msg = "DEGRADED";
    if (!latest.wifiUp)                    msg += " wifi=down";
    if (latest.battPct < BATT_CRIT_PCT)    msg += " battery=critical";
    msg += "\n";
    server.send(503, "text/plain", msg);
  }
}

void handleNotFound() {
  server.send(404, "text/plain",
    "404 Not Found\n"
    "Available endpoints:\n"
    "  GET /              dashboard\n"
    "  GET /data          JSON\n"
    "  GET /health        liveness\n"
    "  GET /reboot        restart board\n"
    "  GET /ota-error     last OTA error\n"
    "  GET /update?url=   HTTP OTA\n"
  );
}

// GET /reboot  — escape hatch if stuck
void handleReboot() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "Rebooting...\n");
  delay(200);
  ESP.restart();
}

// GET /ota-error  — returns last OTA result string
void handleOtaError() {
  server.send(200, "text/plain", otaLastError + "\n");
}

// GET /update?url=http://<mac-ip>:8080/<sketch>.ino.bin
void handleUpdate() {
  String url = server.arg("url");
  if (url.isEmpty()) {
    server.send(400, "text/plain",
      "Usage: /update?url=http://<mac-ip>:8080/<sketch>.ino.bin\n"
      "Serve binary: cd /tmp/esp32build && python3 -m http.server 8080\n");
    return;
  }
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "Starting HTTP OTA from:\n" + url + "\n");
  delay(300);

  otaInProgress = true;
  otaLastError  = "in-progress";
  setLedState(LED_OTA_PROGRESS);

  // Use HTTPClient + Update directly so we can read the exact error string via /ota-error.
  WiFiClient wifiClient;
  HTTPClient http;
  http.begin(wifiClient, url);
  int httpCode = http.GET();

  if (httpCode != 200) {
    otaLastError = "HTTP GET failed: " + String(httpCode);
    Serial.printf("[OTA] %s\n", otaLastError.c_str());
    http.end();
    otaInProgress = false;
    setLedState(latest.wifiUp ? LED_HEALTHY : LED_WIFI_LOST);
    return;
  }

  int contentLen = http.getSize();
  Serial.printf("[OTA] Content-Length: %d\n", contentLen);

  if (!Update.begin(contentLen > 0 ? contentLen : UPDATE_SIZE_UNKNOWN)) {
    otaLastError = "Update.begin failed: " + String(Update.errorString());
    Serial.printf("[OTA] %s\n", otaLastError.c_str());
    http.end();
    otaInProgress = false;
    setLedState(latest.wifiUp ? LED_HEALTHY : LED_WIFI_LOST);
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[512];
  int totalWritten = 0;

  while (http.connected() || stream->available()) {
    size_t avail = stream->available();
    if (avail == 0) { delay(1); continue; }
    int toRead = min((size_t)sizeof(buf), avail);
    int bytesRead = stream->readBytes(buf, toRead);
    if (bytesRead > 0) {
      int written = Update.write(buf, bytesRead);
      if (written != bytesRead) {
        otaLastError = "Update.write failed at byte " + String(totalWritten) + ": " + String(Update.errorString());
        break;
      }
      totalWritten += written;
      // Brighten blue with progress
      if (contentLen > 0) {
        otaBrightness = (uint8_t)map(totalWritten * 100 / contentLen, 0, 100, 20, 220);
        pixel.setPixelColor(0, pixel.Color(0, 0, otaBrightness)); pixel.show();
      }
    }
    if (contentLen > 0 && totalWritten >= contentLen) break;
  }
  http.end();

  if (otaLastError == "in-progress") {
    if (Update.end(true)) {
      otaLastError = "OK";
      Serial.printf("[OTA] Done! %d bytes. Rebooting...\n", totalWritten);
      // 5 purple flashes
      for (int i = 0; i < 5; i++) {
        pixel.setPixelColor(0, pixel.Color(160, 0, 220)); pixel.show(); delay(150);
        pixel.clear(); pixel.show(); delay(100);
      }
      ESP.restart();
    } else {
      otaLastError = "Update.end failed: " + String(Update.errorString());
    }
  }

  Serial.printf("[OTA] FAILED: %s (wrote %d bytes)\n", otaLastError.c_str(), totalWritten);
  Update.abort();
  otaInProgress = false;
  setLedState(latest.wifiUp ? LED_HEALTHY : LED_WIFI_LOST);
}

// ================================================================
// Init helpers
// ================================================================

void initNeoPixel() {
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
  delay(5);
  pixel.begin();
  pixel.setBrightness(80);
  pixel.clear();
  pixel.show();
  setLedState(LED_BOOTING);
}

void initSHT45() {
  Serial.print("[SHT45]   ");
  if (!sht4.begin()) {
    Serial.println("NOT FOUND. Halting.");
    while (1) { pixel.setPixelColor(0, col(255, 0, 0)); pixel.show(); delay(200); pixel.clear(); pixel.show(); delay(200); }
  }
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);
  Serial.println("OK");
}

void initBattery() {
  Serial.print("[BATTERY] ");
  if (!maxlipo.begin()) {
    Serial.println("NOT FOUND. Halting.");
    while (1) { pixel.setPixelColor(0, col(255, 0, 0)); pixel.show(); delay(200); pixel.clear(); pixel.show(); delay(200); }
  }
  Serial.print("OK  (ID: 0x");
  Serial.print(maxlipo.getChipID(), HEX);
  Serial.println(")");
}

void connectWiFi() {
  Serial.print("[WI-FI]   Connecting to \"");
  Serial.print(WIFI_SSID); Serial.print("\" ");
  setLedState(LED_WIFI_CONNECTING);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    updateLed(); delay(500); Serial.print("."); attempts++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WI-FI]   Connected  IP: ");
    Serial.print(WiFi.localIP());
    Serial.print("  RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
    setLedState(LED_WIFI_CONNECTED);
  } else {
    Serial.println("[WI-FI]   FAILED — no Wi-Fi.");
    setLedState(LED_WIFI_LOST);
  }
}

void initOTA() {
  // HTTP OTA configured per-request in handleUpdate() — nothing to init here.
  Serial.println("[HTTP OTA] Ready.");
  Serial.print  ("[HTTP OTA] Endpoint: http://");
  Serial.print  (WiFi.localIP());
  Serial.println("/update?url=http://<mac-ip>:8080/<sketch>.ino.bin");
}

void startServer() {
  server.on("/",          HTTP_GET, handleRoot);
  server.on("/data",      HTTP_GET, handleData);
  server.on("/health",    HTTP_GET, handleHealth);
  server.on("/reboot",    HTTP_GET, handleReboot);
  server.on("/ota-error", HTTP_GET, handleOtaError);
  server.on("/update",    HTTP_GET, handleUpdate);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.print("[HTTP]    Server started on port ");
  Serial.println(HTTP_PORT);
  Serial.println();
  Serial.println("  Try from your Mac:");
  Serial.print  ("    curl http://"); Serial.print(WiFi.localIP()); Serial.println("/");
  Serial.print  ("    curl http://"); Serial.print(WiFi.localIP()); Serial.println("/data");
}

void setup() {
  Serial.begin(115200);
  unsigned long t = millis();
  while (!Serial && (millis() - t) < 3000) delay(10);

  initNeoPixel();
  Wire.begin();

  Serial.println();
  Serial.println("================================================");
  Serial.println("  ESP32-S3 Feather — Phase 6 (HTTP Server)");
  Serial.println("================================================");
  Serial.print("  Built: "); Serial.println(__DATE__ " " __TIME__);
  Serial.println("------------------------------------------------");

  initSHT45();
  initBattery();
  connectWiFi();
  initOTA();
  startServer();

  Serial.println("------------------------------------------------");
  Serial.println("  Reading every 5 s. HTTP server running.");
  Serial.println("================================================");
  Serial.println();
}

// ================================================================

void loop() {
  // ---- Pause sensors while HTTP OTA is downloading/flashing ----
  if (otaInProgress) { updateLed(); return; }

  // ---- Handle any incoming HTTP requests (non-blocking) ----
  server.handleClient();

  // ---- LED ----
  updateLed();

  // ---- Sensor read every READ_INTERVAL_MS ----
  static uint32_t lastRead = 0;
  uint32_t now = millis();
  if (now - lastRead < READ_INTERVAL_MS) return;
  lastRead = now;

  // --- SHT45 ---
  sensors_event_t humid_ev, temp_ev;
  sht4.getEvent(&humid_ev, &temp_ev);

  // --- Battery ---
  float battPct = constrain(maxlipo.cellPercent(), 0.0f, 100.0f);

  // --- Rolling average for temperature ---
  static float tempBuf[TEMP_AVG_SAMPLES] = {};
  static uint8_t tempIdx = 0;
  static uint8_t tempCount = 0;
  float newTempF = (temp_ev.temperature * 9.0f / 5.0f) + 32.0f;
  tempBuf[tempIdx] = newTempF;
  tempIdx = (tempIdx + 1) % TEMP_AVG_SAMPLES;
  if (tempCount < TEMP_AVG_SAMPLES) tempCount++;
  float tempSum = 0;
  for (uint8_t i = 0; i < tempCount; i++) tempSum += tempBuf[i];

  // --- Update shared state ---
  latest.tempF    = newTempF;
  latest.tempFAvg = tempSum / tempCount;
  latest.humidity = humid_ev.relative_humidity;
  latest.battV    = maxlipo.cellVoltage();
  latest.battPct  = battPct;
  latest.wifiUp   = (WiFi.status() == WL_CONNECTED);
  latest.rssi     = latest.wifiUp ? WiFi.RSSI() : 0;
  latest.uptime   = now / 1000UL;
  latest.reading++;

  if (!latest.wifiUp) WiFi.reconnect();

  // --- LED state ---
  if      (battPct < BATT_CRIT_PCT)  { if (ledState != LED_BATT_CRITICAL) setLedState(LED_BATT_CRITICAL); }
  else if (battPct < BATT_LOW_PCT)   { if (ledState != LED_BATT_LOW)      setLedState(LED_BATT_LOW); }
  else if (!latest.wifiUp)           { if (ledState != LED_WIFI_LOST)     setLedState(LED_WIFI_LOST); }
  else {
    if (ledState == LED_WIFI_LOST)   setLedState(LED_WIFI_CONNECTED);
    if (ledState == LED_HEALTHY)     flashReadIndicator();
  }

  // --- Serial ---
  Serial.print("=== Reading #"); Serial.print(latest.reading);
  Serial.print("  (uptime: "); Serial.print(latest.uptime); Serial.println(" s) ===");
  Serial.print("  Temperature  : "); Serial.print(latest.tempF,    1); Serial.println(" F");
  Serial.print("  Humidity     : "); Serial.print(latest.humidity, 1); Serial.println(" %RH");
  Serial.print("  Batt Voltage : "); Serial.print(latest.battV,    3); Serial.println(" V");
  Serial.print("  Batt SOC     : "); Serial.print(latest.battPct,  1);
  if      (battPct < BATT_CRIT_PCT) Serial.println(" %  [CRITICAL]");
  else if (battPct < BATT_LOW_PCT)  Serial.println(" %  [LOW]");
  else                              Serial.println(" %");
  if (latest.wifiUp) {
    Serial.print("  Wi-Fi        : Connected  IP: "); Serial.print(WiFi.localIP());
    Serial.print("  RSSI: "); Serial.print(latest.rssi); Serial.println(" dBm");
  } else {
    Serial.println("  Wi-Fi        : Not connected (reconnecting...)");
  }
  Serial.println();
}
