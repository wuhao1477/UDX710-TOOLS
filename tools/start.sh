#!/bin/sh

set -eu

BASE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SERVER="$BASE_DIR/server"
WATCHER="$BASE_DIR/rj45_bridge_watch.sh"
SERVER_PID="/tmp/udx710-tools.pid"
WATCHER_PID="/tmp/udx710-rj45-daemon.pid"

if [ ! -x "$SERVER" ]; then
    echo "server not found or not executable: $SERVER" >&2
    exit 1
fi

if ! command -v start-stop-daemon >/dev/null 2>&1; then
    echo "start-stop-daemon is required on the target device" >&2
    exit 1
fi

cd "$BASE_DIR"
server_running=0
if [ -r "$SERVER_PID" ]; then
    pid=$(cat "$SERVER_PID" 2>/dev/null || true)
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        server_running=1
    fi
fi
if [ "$server_running" -eq 0 ]; then
    start-stop-daemon -S -b -m -p "$SERVER_PID" -x "$SERVER" -- 6677
fi

if [ -x "$WATCHER" ]; then
    start-stop-daemon -S -b -m -p "$WATCHER_PID" -a /bin/sh -- \
        -c "exec '$WATCHER'"
fi

echo "UDX710 tools started on port 6677"
