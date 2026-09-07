#!/bin/sh
set -eu

for route in /api/auth/login /api/apn/config /api/plugins /api/rathole/status /api/ipv6-proxy/status /api/netif/list /api/usb/mode; do
  grep -q "\"$route\"" src/handlers/http_server.c
done

for route in /api/capabilities /api/led/status /api/led/control /api/wifi/status /api/wifi/config /api/wifi/clients /api/factory-reset; do
  grep -q "\"$route\"" src/handlers/http_server.c
done
