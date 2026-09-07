#!/bin/sh
set -eu

for route in /api/auth/login /api/apn/config /api/plugins /api/rathole/status /api/ipv6-proxy/status /api/netif/list /api/usb/mode; do
  grep -q "\"$route\"" src/handlers/http_server.c
done

for route in /api/capabilities /api/led/status /api/led/control /api/wifi/status /api/wifi/config /api/wifi/clients /api/factory-reset; do
  grep -q "\"$route\"" src/handlers/http_server.c
done

for route in /api/builtin-cards /api/builtin-cards/realname /api/builtin-cards/switch; do
  grep -q "\"$route\"" src/handlers/http_server.c
done

for route in /api/adb/status /api/adb/wireless /api/adb/usb /api/adb/restart; do
  grep -q "\"$route\"" src/handlers/http_server.c
done

test -f tools/start.sh
test -f tools/rj45_bridge_watch.sh
grep -q 'tools/start.sh' .github/workflows/build.yml
grep -q 'tools/rj45_bridge_watch.sh' .github/workflows/build.yml
