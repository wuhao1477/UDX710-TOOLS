#!/bin/sh

set -eu

BASE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SERVER="$BASE_DIR/server"
WATCHER="$BASE_DIR/rj45_bridge_watch.sh"
SERVER_PID="/tmp/udx710-tools.pid"
WATCHER_PID="/tmp/udx710-rj45-bridge.pid"

if [ ! -x "$SERVER" ]; then
    echo "server not found or not executable: $SERVER" >&2
    exit 1
fi

if ! command -v start-stop-daemon >/dev/null 2>&1; then
    echo "start-stop-daemon is required on the target device" >&2
    exit 1
fi

cd "$BASE_DIR"
start-stop-daemon -S -b -m -p "$SERVER_PID" -x "$SERVER" -- 6677

if [ -x "$WATCHER" ]; then
    start-stop-daemon -S -b -m -p "$WATCHER_PID" -a /bin/sh -- \
        -c "exec '$WATCHER'"
fi

echo "UDX710 tools started on port 6677"
