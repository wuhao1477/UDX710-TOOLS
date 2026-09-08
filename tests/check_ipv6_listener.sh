#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
makefile="$repo_root/src/Makefile"
server="$repo_root/src/handlers/http_server.c"

grep -Eq 'CFLAGS = .* -DMG_ENABLE_IPV6=1([[:space:]]|$)' "$makefile"
grep -Eq 'CFLAGS = .* -DMG_IPV6_V6ONLY=1([[:space:]]|$)' "$makefile"
grep -Fq '"http://0.0.0.0:%s"' "$server"
grep -Fq '"http://[::]:%s"' "$server"
test "$(grep -c 'mg_http_listen(&g_mgr' "$server")" -eq 2
