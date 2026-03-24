#!/bin/zsh
# ============================================================
# Compile + OTA Flash — ESP32-S3 Feather
# ============================================================
# Usage: ./run.sh [path/to/sketch_dir]
#
# This is the ONE command to build and deploy firmware.
# Always run this manually — never delegate to automation.
#   ./run.sh                        → builds firmware/phase6_http_server
#   ./run.sh firmware/my_sketch     → builds a specific sketch
# ============================================================

BOARD_IP="10.0.0.151"
MAC_IP="10.0.0.162"
HTTP_PORT="8080"
FQBN="esp32:esp32:adafruit_feather_esp32s3:CDCOnBoot=cdc,PartitionScheme=min_spiffs"
BUILD_DIR="/tmp/esp32build"

SKETCH_DIR="${1:-$(dirname "$0")/firmware/phase6_http_server}"
SKETCH_NAME="$(basename "$SKETCH_DIR").ino.bin"

echo "========================================"
echo "  ESP32-S3 OTA Flash"
echo "  Sketch : $SKETCH_DIR"
echo "  Board  : $BOARD_IP"
echo "========================================"

# ---- Step 1: Compile ----
echo ""
echo "[1/3] Compiling..."
arduino-cli compile \
  --fqbn "$FQBN" \
  --build-path "$BUILD_DIR" \
  "$SKETCH_DIR"

if [[ $? -ne 0 ]]; then
  echo "ERROR: Compile failed. Aborting."
  exit 1
fi
echo "      Compile OK — $BUILD_DIR/$SKETCH_NAME"

# ---- Step 2: Start HTTP server (kill any existing one first) ----
echo ""
echo "[2/3] Starting HTTP server on port $HTTP_PORT..."
python3 -c "
import subprocess, time, os
r = subprocess.run(['lsof', '-ti', 'tcp:$HTTP_PORT'], capture_output=True, text=True)
for pid in r.stdout.strip().split():
    try: os.kill(int(pid), 15); print(f'  Stopped old server PID {pid}')
    except: pass
time.sleep(0.8)
"
python3 /tmp/serve_ota.py "$BUILD_DIR/$SKETCH_NAME" "$HTTP_PORT" &
HTTP_PID=$!
sleep 1

# verify server started
if ! python3 -c "import socket; s=socket.socket(); s.settimeout(2); s.connect(('127.0.0.1', $HTTP_PORT)); s.close()" 2>/dev/null; then
  echo "ERROR: HTTP server failed to start."
  exit 1
fi
echo "      HTTP server running (PID $HTTP_PID)"

# ---- Step 3: Trigger OTA ----
echo ""
echo "[3/3] Triggering OTA flash on $BOARD_IP..."
OTA_URL="http://${MAC_IP}:${HTTP_PORT}/${SKETCH_NAME}"
RESPONSE=$(python3 -c "
import urllib.request
try:
    r = urllib.request.urlopen('http://${BOARD_IP}/update?url=${OTA_URL}', timeout=15)
    print(r.read().decode().strip())
except Exception as e:
    print('ERROR: ' + str(e))
    exit(1)
")
echo "      Board responded: $RESPONSE"

if [[ "$RESPONSE" == ERROR* ]]; then
  echo "OTA trigger failed."
  exit 1
fi

# ---- Poll for board readiness after reboot ----
echo ""
echo "      Polling for board (up to 90s)..."
HEALTH=""
for i in $(seq 1 45); do
  sleep 2
  HEALTH=$(python3 -c "
import urllib.request
try:
    r = urllib.request.urlopen('http://${BOARD_IP}/health', timeout=3)
    print(str(r.status) + ' ' + r.read().decode().strip())
except: pass
" 2>/dev/null)
  if [[ "$HEALTH" == 200* ]]; then
    echo ""
    echo "      Board ready after ~$((i * 2))s"
    break
  fi
  printf "."
done

echo ""
echo "  /health  -> ${HEALTH:-TIMEOUT}"

DATA=$(python3 -c "
import urllib.request
try:
    r = urllib.request.urlopen('http://${BOARD_IP}/data', timeout=8)
    print(r.read().decode().strip())
except Exception as e:
    print('ERROR: ' + str(e))
")
echo "  /data    -> $DATA"

echo ""
if [[ "$HEALTH" == 200* ]]; then
  echo "  Flash successful! Board is live at http://${BOARD_IP}/"
else
  echo "  Board did not respond — may still be connecting. Try: http://${BOARD_IP}/health"
fi
echo "========================================"
