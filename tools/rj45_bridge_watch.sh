#!/bin/sh

set -u

NET_ROOT=${RJ45_SYS_CLASS_NET:-/sys/class/net}
BRIDGE=${RJ45_BRIDGE:-tether}
PID_FILE=${RJ45_BRIDGE_PID:-/tmp/udx710-rj45-bridge.pid}

find_wired_iface() {
    for path in "$NET_ROOT"/eth* "$NET_ROOT"/en* "$NET_ROOT"/lan*; do
        [ -e "$path" ] || continue
        name=${path##*/}
        case "$name" in
            usb*|sipa_*|wlan*|tether) continue ;;
        esac
        printf '%s\n' "$name"
        return 0
    done
    return 1
}

bridge_once() {
    [ -d "$NET_ROOT/$BRIDGE/bridge" ] || return 0
    iface=$(find_wired_iface 2>/dev/null) || return 0
    [ -e "$NET_ROOT/$iface/brport" ] && return 0
    ifconfig "$iface" up 2>/dev/null || return 0
    brctl addif "$BRIDGE" "$iface" 2>/dev/null || return 0
    printf '%s\n' "bridged $iface into $BRIDGE"
}

if [ "${RJ45_BRIDGE_WATCH_TEST:-0}" = 1 ]; then
    bridge_once
    exit 0
fi

if [ -r "$PID_FILE" ]; then
    old_pid=$(cat "$PID_FILE" 2>/dev/null || true)
    if [ -n "$old_pid" ] && kill -0 "$old_pid" 2>/dev/null; then
        exit 0
    fi
fi

echo $$ > "$PID_FILE" || exit 1
trap 'rm -f "$PID_FILE"' EXIT
trap 'exit 0' HUP INT TERM

while :; do
    bridge_once
    sleep 3
done
