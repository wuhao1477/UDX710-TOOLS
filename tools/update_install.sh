#!/bin/sh

set -eu

BASE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TARGET_DIR=${UDX710_TARGET_DIR:-/home/root/6677}
SERVER_PID=/tmp/udx710-tools.pid
WATCHER_PID=/tmp/udx710-rj45-daemon.pid

stop_pid_file() {
    pid_file=$1
    if [ ! -r "$pid_file" ]; then
        return 0
    fi
    pid=$(cat "$pid_file" 2>/dev/null || true)
    if [ -z "$pid" ] || ! kill -0 "$pid" 2>/dev/null; then
        return 0
    fi
    kill "$pid" 2>/dev/null || true
    attempt=0
    while kill -0 "$pid" 2>/dev/null && [ "$attempt" -lt 10 ]; do
        sleep 1
        attempt=$((attempt + 1))
    done
}

if [ ! -x "$BASE_DIR/server" ] || [ ! -d "$BASE_DIR/dist" ]; then
    echo "update package is incomplete" >&2
    exit 1
fi

stop_pid_file "$SERVER_PID"
stop_pid_file "$WATCHER_PID"
mkdir -p "$TARGET_DIR"

cp "$BASE_DIR/server" "$TARGET_DIR/.server.new"
cp "$BASE_DIR/start.sh" "$TARGET_DIR/.start.sh.new"
cp "$BASE_DIR/rj45_bridge_watch.sh" "$TARGET_DIR/.rj45_bridge_watch.sh.new"
chmod 755 "$TARGET_DIR/.server.new" "$TARGET_DIR/.start.sh.new" \
    "$TARGET_DIR/.rj45_bridge_watch.sh.new"
cp -R "$BASE_DIR/dist" "$TARGET_DIR/.dist.new"

rm -f "$TARGET_DIR/server" "$TARGET_DIR/start.sh" \
    "$TARGET_DIR/rj45_bridge_watch.sh"
rm -rf "$TARGET_DIR/dist"
mv "$TARGET_DIR/.server.new" "$TARGET_DIR/server"
mv "$TARGET_DIR/.start.sh.new" "$TARGET_DIR/start.sh"
mv "$TARGET_DIR/.rj45_bridge_watch.sh.new" "$TARGET_DIR/rj45_bridge_watch.sh"
mv "$TARGET_DIR/.dist.new" "$TARGET_DIR/dist"

cd "$TARGET_DIR"
./start.sh
echo "UDX710 tools updated; existing database and plugin data were preserved"
