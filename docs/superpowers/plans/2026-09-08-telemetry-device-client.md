# Device Telemetry Upload Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Add an opt-in, best-effort telemetry uploader to UDX710-TOOLS that sends complete non-sensitive device snapshots, events, and logs to a configurable Go receiver.

**Architecture:** Add a telemetry C module with a worker thread, direct calls to existing collectors, a shared redaction/record codec, and curl-based gzip NDJSON uploads. Expose authenticated device APIs and a small Vue settings component; do not add a local retry queue or a new device-side daemon.

**Tech Stack:** C11, existing GLib/Mongoose helpers, SQLite CLI-backed config table, /usr/bin/curl, Vue 3 Composition API, existing i18n and test Makefile.

**Spec:** docs/superpowers/specs/2026-09-08-device-telemetry-upload-design.md

## Global Constraints

- Upload defaults to disabled and uses the existing 6677.db config table.
- The default snapshot interval is 60 seconds; accepted values are 10–3600 seconds.
- The device sends all available non-sensitive data, but never sends passwords, credentials, Tokens, or SMS正文.
- There is no durable offline queue and no total upload-size cap; failed fragments are discarded.
- Transport uses gzip NDJSON fragments of approximately 1 MiB; fragment size is not a retention limit.
- The main Mongoose/GLib/D-Bus loop must not wait on collection or upload work.
- Reuse the existing /usr/bin/curl and parameter-array process execution; do not add a network library or a new resident process.

## File Map

- Create src/include/system/telemetry.h: public configuration, status, lifecycle, capture, and event APIs.
- Create src/system/telemetry.c: worker lifecycle, collectors, log capture, fragment creation, curl sender, and in-memory status.
- Create src/system/telemetry_codec.c: bounded record construction and redaction helpers with no threads or database dependencies.
- Create tests/telemetry_codec_test.c: host-side redaction and envelope tests.
- Create web/src/components/TelemetryUpload.vue: settings form and status display.
- Modify src/Makefile, src/main.c, src/handlers/http_server.c, src/handlers/handlers.c, src/include/handlers/handlers.h.
- Modify src/system/ofono.c, src/system/airplane.c, src/system/exec_utils.c for central capture.
- Modify web/src/components/SystemSettings.vue, web/src/composables/useApi.js, both locale files, tests/Makefile, and tests/check_routes.sh.

### Task 1: Add failing codec tests

**Files:** Create tests/telemetry_codec_test.c; modify tests/Makefile.

**Interfaces:** The tests consume telemetry_redact_text() and telemetry_build_record() and produce assertions for secret removal, SMS body removal, envelope fields, and small-buffer rejection.

- [ ] **Step 1: Write the failing tests**

~~~c
static void test_redacts_secrets(void) {
  char out[512];
  assert(telemetry_redact_text(
      "password=wifi token=abc state=ready", out, sizeof(out)) == 0);
  assert(strstr(out, "wifi") == NULL);
  assert(strstr(out, "abc") == NULL);
  assert(strstr(out, "state=ready") != NULL);
}

static void test_removes_sms_body(void) {
  char out[512];
  assert(telemetry_redact_text(
      "event=sms_received body=secret text sender=10086", out,
      sizeof(out)) == 0);
  assert(strstr(out, "secret text") == NULL);
  assert(strstr(out, "sender=10086") != NULL);
}

static void test_builds_record(void) {
  char out[1024];
  assert(telemetry_build_record(out, sizeof(out), "SN-1", "boot-1", 7,
                                "snapshot", "system",
                                "{\"cpu_usage\":12.5}") == 0);
  assert(strstr(out, "\"device_id\":\"SN-1\"") != NULL);
  assert(strstr(out, "\"sequence\":7") != NULL);
  assert(strstr(out, "cpu_usage") != NULL);
}

static void test_rejects_small_buffer(void) {
  char out[16];
  assert(telemetry_build_record(out, sizeof(out), "SN-1", "boot-1", 1,
                                "event", "system", "{}") != 0);
}
~~~

- [ ] **Step 2: Add telemetry_codec_test to the test target and run make -C tests telemetry_codec_test. Expected result: compilation fails because the codec does not exist.**
- [ ] **Step 3: Commit the red test.**

~~~bash
git add tests/telemetry_codec_test.c tests/Makefile
git commit -S -m "test(telemetry): 增加脱敏协议测试"
~~~

### Task 2: Implement the codec

**Files:** Create src/include/system/telemetry.h and src/system/telemetry_codec.c; modify tests/Makefile.

**Interfaces:** Produce int telemetry_redact_text(const char *, char *, size_t) and int telemetry_build_record(char *, size_t, const char *, const char *, unsigned long long, const char *, const char *, const char *).

- [ ] **Step 1: Implement length-checked C11 helpers.** Redact case-insensitive keys containing password, passwd, pwd, token, secret, authorization, cookie, api_key, access_key, or private_key; redact Bearer/Basic values and SMS body text. Never log an input string on an error path.
- [ ] **Step 2: Build the envelope with escaped string fields and caller-provided JSON payload; reject truncation.**
- [ ] **Step 3: Run make -C tests telemetry_codec_test and ./tests/telemetry_codec_test; expected result: all assertions pass.**
- [ ] **Step 4: Commit.**

~~~bash
git add src/include/system/telemetry.h src/system/telemetry_codec.c tests/Makefile
git commit -S -m "feat(telemetry): 增加记录编解码与脱敏"
~~~

### Task 3: Add configuration, worker lifecycle, and fragments

**Files:** Create src/system/telemetry.c; modify src/include/system/telemetry.h and src/Makefile.

**Interfaces:** Consume existing db_*, config_*, run_command_argv, get_serial, and get_uptime helpers. Produce telemetry_init(const char *), telemetry_deinit(void), telemetry_get_config(TelemetryConfig *), telemetry_save_config(const TelemetryConfig *, int), telemetry_get_status(TelemetryStatus *), telemetry_test(const TelemetryConfig *), telemetry_capture_line(const char *, const char *), telemetry_capture_at(const char *, const char *, int), and telemetry_capture_command(const char *, const char *, int).

- [ ] **Step 1: Define fixed-size TelemetryConfig and TelemetryStatus.** Validate URL scheme, non-empty Token when enabling, serial availability, and interval bounds. A missing Token in a POST preserves the stored value; clear_token removes it.
- [ ] **Step 2: Load config from 6677.db, create one worker and condition variable, and make the worker collect an initial snapshot/history when enabled before waiting for the configured interval.**
- [ ] **Step 3: Send gzip NDJSON through a pipe to /usr/bin/curl with Authorization, Content-Type, Content-Encoding, -f, and --max-time 30. Treat only exit code zero as success and discard every failed fragment.**
- [ ] **Step 4: Add pure configuration validation cases to telemetry_codec_test.c; run make -C tests test.**
- [ ] **Step 5: Commit.**

~~~bash
git add src/system/telemetry.c src/include/system/telemetry.h src/Makefile tests/telemetry_codec_test.c
git commit -S -m "feat(telemetry): 增加设备上传线程"
~~~

### Task 4: Add collectors and central log capture

**Files:** Modify src/main.c, src/system/telemetry.c, src/system/ofono.c, src/system/airplane.c, src/system/exec_utils.c, and src/Makefile.

**Interfaces:** Consume existing sysinfo, device_profile, netif, traffic, wifi, wifi_clients, charge, usb_mode, ofono, apn, rathole, ipv6_proxy, and notification APIs. Produce snapshot, event, and service/command/AT/system/dmesg log records.

- [ ] **Step 1: Start a pipe reader before device_profile_init in main, remove -DDISABLE_PRINTF, redirect stdout/stderr after the reader is ready, and drain/discard when disabled. Stop it after telemetry_deinit.**
- [ ] **Step 2: Collect system info, device profile, interfaces, traffic, clients, power, USB/ADB, oFono, APN, Rathole, IPv6, and non-sensitive notification state directly; omit SMS bodies.**
- [ ] **Step 3: Call telemetry_capture_at in execute_at and send_at after the result is known. Call telemetry_capture_command in run_command_argv after child completion and exclude the uploader child by an explicit flag.**
- [ ] **Step 4: Read current system logs and dmesg on enable; retain only in-memory offsets/hashes for incremental lines. Emit a source error event when a source is unavailable.**
- [ ] **Step 5: Run make -C tests test and make -C src; expected result: existing tests pass and the current aarch64 link stage is reached.**
- [ ] **Step 6: Commit.**

~~~bash
git add src/main.c src/system/telemetry.c src/system/ofono.c src/system/airplane.c src/system/exec_utils.c src/Makefile
git commit -S -m "feat(telemetry): 接入设备状态与日志采集"
~~~

### Task 5: Add authenticated device APIs and lifecycle hooks

**Files:** Modify src/include/handlers/handlers.h, src/handlers/handlers.c, src/handlers/http_server.c, src/system/telemetry.c, and tests/check_routes.sh.

**Interfaces:** Produce handle_telemetry_config, handle_telemetry_status, and handle_telemetry_test using the existing auth middleware, HTTP helpers, and JSON builder.

- [ ] **Step 1: Add GET/POST /api/telemetry/config, GET /api/telemetry/status, and POST /api/telemetry/test after the existing settings routes; keep them behind authentication.**
- [ ] **Step 2: Return only enabled, url, interval_sec, and token_configured from GET. Parse enabled, url, interval_sec, token, and clear_token in POST and return sanitized config.**
- [ ] **Step 3: Return counters, timestamps, running state, and safe errors from status. The test handler sends HEAD with Authorization and X-Device-ID and returns the receiver status.**
- [ ] **Step 4: Call telemetry_init("6677.db") after the shared database is ready and telemetry_deinit before notification_deinit.**
- [ ] **Step 5: Run ./tests/check_routes.sh and commit.**

~~~bash
git add src/include/handlers/handlers.h src/handlers/handlers.c src/handlers/http_server.c src/system/telemetry.c tests/check_routes.sh
git commit -S -m "feat(telemetry): 增加上传配置接口"
~~~

### Task 6: Add the settings UI

**Files:** Create web/src/components/TelemetryUpload.vue; modify web/src/components/SystemSettings.vue, web/src/composables/useApi.js, both locale files, and tests/frontend_api_test.mjs.

**Interfaces:** Produce getTelemetryConfig(), saveTelemetryConfig(config), getTelemetryStatus(), and testTelemetry(config) exports and a settings component mounted by SystemSettings.vue.

- [ ] **Step 1: Add API helpers through the existing request wrapper and add typeof assertions; run node tests/frontend_api_test.mjs and confirm it fails before exports exist.**
- [ ] **Step 2: Implement the component. Load config/status, keep the Token input blank, preserve stored Token when blank, provide clear, validate 10–3600, show HTTP warning, save, test, refresh status, and display only safe errors.**
- [ ] **Step 3: Mount the component inside SystemSettings without a new top-level menu.**
- [ ] **Step 4: Add Chinese and English labels, descriptions, validation, status, and warning strings. Run node tests/frontend_api_test.mjs and cd web && pnpm run build.**
- [ ] **Step 5: Commit.**

~~~bash
git add web/src/components/TelemetryUpload.vue web/src/components/SystemSettings.vue web/src/composables/useApi.js web/src/i18n/locales/zh-CN.js web/src/i18n/locales/en-US.js tests/frontend_api_test.mjs
git commit -S -m "feat(telemetry): 增加上传设置页面"
~~~

### Task 7: Package, document, and verify

**Files:** Modify src/packed_fs.c only through the existing generator; modify README.md, README_CN.md, and tests/check_routes.sh as needed.

- [ ] **Step 1: Regenerate packed frontend assets using the repository’s documented command; do not hand-edit packed_fs.c.**
- [ ] **Step 2: Run make -C tests test, node tests/frontend_api_test.mjs, ./tests/check_routes.sh, cd web && pnpm run build, and cd ../src && make.**
- [ ] **Step 3: Search changed files for sample Tokens, passwords, private keys, Authorization values, SMS body fixtures, and accidental local queues.**
- [ ] **Step 4: Commit docs/generated assets.**

~~~bash
git add src/packed_fs.c README.md README_CN.md tests/check_routes.sh
git commit -S -m "docs(telemetry): 更新上传功能说明"
~~~

- [ ] **Step 5: Verify git status --short --branch and git log -5 --oneline --decorate; leave no generated binaries or local databases.**
