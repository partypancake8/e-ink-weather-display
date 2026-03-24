/*
 * ============================================================
 * Phase 7 — HTTP OTA (pull model)
 * Adafruit ESP32-S3 Feather
 * ============================================================
 *
 * ArduinoOTA uses a "push" model: the board connects BACK to your
 * Mac over TCP. Many home routers block device-to-device TCP
 * (AP/client isolation), causing OTA_CONNECT_ERROR even though
 * curl to the board works fine.
 *
 * This sketch uses the HTTP OTA "pull" model instead:
 *   1. Mac serves the .bin over HTTP (python3 -m http.server 8080)
 *   2. curl triggers the board to download and flash it
 *
 * The board initiates all connections — same direction as normal
 * internet traffic — so routers never block it.
 *
 * ── Workflow ────────────────────────────────────────────────
 *
 * Step 1: USB-flash this sketch once:
 *   arduino-cli upload --fqbn esp32:esp32:adafruit_feather_esp32s3:CDCOnBoot=cdc \
 *       -p /dev/cu.usbmodem2101 --input-dir /tmp/esp32build_p7
 *
 * Step 2: Compile whatever sketch you want to flash OTA:
 *   arduino-cli compile --fqbn ... --build-path /tmp/esp32build <sketch>
 *
 * Step 3: Serve it from your Mac:
 *   cd /tmp/esp32build_p7 && python3 -m http.server 8080
 *
 * Step 4: Trigger OTA from any terminal / browser:
 *   curl "http://10.0.0.151/update?url=http://10.0.0.162:8080/phase7_ota_test.ino.bin"
 *
 * Step 5 (diagnose): Test board→Mac TCP directly:
 *   # In terminal: nc -l 9999
 *   # Then:       curl "http://10.0.0.151/tcp-test?ip=10.0.0.162&port=9999"
 *   # SUCCEEDED = router allows it; FAILED = AP isolation blocking ArduinoOTA
 *
 * ── Endpoints ───────────────────────────────────────────────
 *   GET /health            — "OK\n"
 *   GET /tcp-test?ip=&port= — test outbound TCP, returns SUCCEEDED/FAILED
 *   GET /update?url=       — trigger HTTP OTA from given .bin URL
 * ============================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdate.h>
#include <Adafruit_NeoPixel.h>

const char* WIFI_SSID     = "FBI Van";
const char* WIFI_PASSWORD = "sambearrosie";

static Adafruit_NeoPixel pixel(NEOPIXEL_NUM, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static WebServer server(80);

static void setPixel(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

// ================================================================
// HTTP handlers
// ================================================================

void handleHealth() {
  server.send(200, "text/plain", "OK\n");
}

// Tests whether the board can initiate an outbound TCP connection.
// Usage: curl "http://<board-ip>/tcp-test?ip=10.0.0.162&port=9999"
// Run "nc -l 9999" on your Mac first so there is something to connect to.
void handleTcpTest() {
  String ip   = server.arg("ip");
  String port_s = server.arg("port");
  if (ip.isEmpty() || port_s.isEmpty()) {
    server.send(400, "text/plain",
      "Usage: /tcp-test?ip=<host-ip>&port=<port>\n"
      "Start 'nc -l <port>' on your Mac first.\n");
    return;
  }
  int port = port_s.toInt();
  WiFiClient client;
  client.setTimeout(5000);
  bool ok = client.connect(ip.c_str(), port);
  if (ok) {
    client.stop();
    server.send(200, "text/plain",
      "TCP connect to " + ip + ":" + port_s + " SUCCEEDED\n"
      "ArduinoOTA push-model should work — try espota.py.\n");
  } else {
    server.send(200, "text/plain",
      "TCP connect to " + ip + ":" + port_s + " FAILED\n"
      "Router AP-isolation is blocking device-to-device TCP.\n"
      "Use HTTP OTA pull-model (/update?url=...) instead.\n");
  }
}

// Triggers an HTTP OTA update. The board downloads and flashes the .bin.
// Usage: curl "http://<board-ip>/update?url=http://10.0.0.162:8080/firmware.bin"
// Prerequisite: python3 -m http.server 8080  (in the directory with the .bin)
void handleUpdate() {
  String url = server.arg("url");
  if (url.isEmpty()) {
    server.send(400, "text/plain",
      "Usage: /update?url=http://<mac-ip>:8080/<sketch>.ino.bin\n"
      "First run: cd /tmp/esp32build && python3 -m http.server 8080\n");
    return;
  }

  // Respond immediately so the caller gets feedback before we block.
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", "Starting HTTP OTA from: " + url + "\n");
  // Flush the response to the client before we block on the download.
  delay(200);

  setPixel(0, 0, 180);  // blue = updating

  httpUpdate.onStart([]()  { Serial.println("[HTTP OTA] Start"); });
  httpUpdate.onEnd([]()    { Serial.println("[HTTP OTA] Done — rebooting"); });
  httpUpdate.onError([](int err) {
    Serial.print("[HTTP OTA] Error: ");
    Serial.print(err);
    Serial.print(" — ");
    Serial.println(httpUpdate.getLastErrorString());
    setPixel(200, 0, 0);
  });
  httpUpdate.onProgress([](int cur, int total) {
    uint8_t pct = (uint8_t)(cur * 100 / total);
    pixel.setPixelColor(0, pixel.Color(0, 0, map(pct, 0, 100, 20, 220)));
    pixel.show();
    if (pct % 10 == 0) {
      Serial.print("[HTTP OTA] "); Serial.print(pct); Serial.println("%");
    }
  });

  WiFiClient client;
  // update() blocks until done, then calls ESP.restart() on success.
  t_httpUpdate_return ret = httpUpdate.update(client, url);

  // Only reached on failure (success reboots the board).
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("[HTTP OTA] FAILED(%d): %s\n",
        httpUpdate.getLastError(),
        httpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("[HTTP OTA] Server says no update needed.");
      break;
    default:
      break;
  }
  setPixel(200, 0, 0);  // red = failed
}

// ================================================================

void setup() {
  Serial.begin(115200);
  unsigned long t = millis();
  while (!Serial && millis() - t < 3000) delay(10);

  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, NEOPIXEL_POWER_ON);
  delay(5);
  pixel.begin();
  pixel.setBrightness(80);
  setPixel(30, 30, 30);  // dim white = booting

  Serial.println();
  Serial.println("=== Phase 7: HTTP OTA Test ===");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  Serial.print("IP: "); Serial.println(WiFi.localIP());
  setPixel(0, 60, 0);  // green = ready

  server.on("/health",   HTTP_GET, handleHealth);
  server.on("/tcp-test", HTTP_GET, handleTcpTest);
  server.on("/update",   HTTP_GET, handleUpdate);
  server.begin();

  Serial.println("Endpoints:");
  Serial.print("  http://"); Serial.print(WiFi.localIP()); Serial.println("/health");
  Serial.print("  http://"); Serial.print(WiFi.localIP()); Serial.println("/tcp-test?ip=<mac>&port=<port>");
  Serial.print("  http://"); Serial.print(WiFi.localIP()); Serial.println("/update?url=http://<mac>:8080/<bin>");
  Serial.println("==============================");
}

void loop() {
  server.handleClient();
}
