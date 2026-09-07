#!/bin/sh
set -eu

grep -q 'device_profile_get' src/system/led.c
grep -q 'device_profile_get' src/system/wifi.c
grep -q 'device_profile_get' src/system/factory_reset.c
! grep -q 'WLAN_IFACE.*wlan0' src/system/wifi.c
! grep -q '/sys/class/leds/lte_' src/system/led.c
