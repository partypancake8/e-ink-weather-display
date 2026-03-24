#!/bin/zsh
# ============================================================
# Download sensor history log from ESP32 to this workspace.
# ============================================================
# Usage: ./backup.sh
#
# Saves to: data/history_<timestamp>.csv
# Also overwrites data/history_latest.csv for easy reference.
# ============================================================

BOARD_IP="10.0.0.151"
OUT_DIR="$(dirname "$0")/data"
TIMESTAMP="$(date +%Y%m%d_%H%M%S)"
OUT_FILE="$OUT_DIR/history_${TIMESTAMP}.csv"
LATEST="$OUT_DIR/history_latest.csv"

mkdir -p "$OUT_DIR"

echo "========================================"
echo "  ESP32-S3 History Backup"
echo "  Board  : $BOARD_IP"
echo "  Output : $OUT_FILE"
echo "========================================"

# Download CSV
python3 -c "
import urllib.request, sys

url = 'http://${BOARD_IP}/history.csv'
out = '${OUT_FILE}'

try:
    r = urllib.request.urlopen(url, timeout=15)
    data = r.read()
    with open(out, 'wb') as f:
        f.write(data)
    lines = data.decode().strip().splitlines()
    # subtract 1 for header
    entries = max(0, len(lines) - 1)
    print(f'  Downloaded {len(data):,} bytes  ({entries} entries)')
except Exception as e:
    print(f'  ERROR: {e}')
    sys.exit(1)
"

if [[ $? -ne 0 ]]; then
  echo "  Backup failed."
  exit 1
fi

# Also overwrite the 'latest' symlink equivalent
cp "$OUT_FILE" "$LATEST"

echo "  Latest copy: $LATEST"
echo "========================================"
echo ""
echo "  View: cat $LATEST"
echo "  Or open http://${BOARD_IP}/history.csv in your browser."
echo "========================================"
