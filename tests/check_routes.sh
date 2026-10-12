#!/bin/sh
set -eu

for route in /api/builtin-cards /api/builtin-cards/realname \
  /api/builtin-cards/switch /api/goform/proxy; do
  grep -q "\"$route\"" src/handlers/http_server.c
done

grep -q 'system/builtin_card_rules.c' src/Makefile
grep -q 'system/goform_client.c' src/Makefile
grep -q 'system/builtin_cards.c' src/Makefile
grep -q 'system/goform_proxy.c' src/Makefile
test -f web/src/components/BuiltinCardManager.vue
