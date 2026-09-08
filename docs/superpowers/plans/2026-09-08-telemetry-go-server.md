# Go Telemetry Receiver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Create a standalone udx710-log-server Git repository that authenticates shared group Tokens, accepts streaming gzip NDJSON from many devices, and stores selected records in SQLite or PostgreSQL.

**Architecture:** Use Go net/http and database/sql with a store implementation for each database. Authenticate before reading the request body, decode gzip/NDJSON incrementally, apply type/source policy, and insert records with a cross-database deduplication key.

**Tech Stack:** Go, net/http, encoding/json, compress/gzip, database/sql, pgx stdlib driver, pure-Go SQLite driver, embedded SQL migrations, and the Go testing package.

**Spec:** ../UDX710-TOOLS/docs/superpowers/specs/2026-09-08-device-telemetry-upload-design.md

## Global Constraints

- One group Token may authenticate any number of devices; device_id is the serial number.
- Persist only a SHA-256 Token hash, never plaintext Token.
- POST /v1/ingest accepts gzip NDJSON without a total request-size limit or full-body buffering.
- HEAD /v1/ingest authenticates Authorization and X-Device-ID and returns 204 without inserting a record.
- STORE_TYPES and STORE_SOURCES decide what is stored; defaults save all accepted records.
- Passwords, credentials, Tokens, and SMS正文 must not be stored even if sent accidentally.
- No automatic retention deletion in version one.
- Every SQL statement is parameterized; SQLite and PostgreSQL migrations are separate.
- No web framework, ORM, admin UI, query UI, or alerting system.

## File Map

Create the standalone sibling repository at ../udx710-log-server:

- go.mod: module metadata and pgx/SQLite dependencies.
- cmd/server/main.go: environment loading, store selection, migrations, HTTP server, shutdown.
- cmd/group/main.go: create, disable, and rotate group Tokens.
- internal/protocol/types.go: versioned envelope and response types.
- internal/policy/policy.go: type/source allowlists.
- internal/auth/auth.go: Token hashing and group lookup.
- internal/store/store.go, sql.go, sqlite.go, postgres.go: database contract and backends.
- internal/ingest/handler.go and redact.go: authenticated streaming handler and redaction.
- internal/health/handler.go: /healthz.
- migrations/sqlite/001_init.sql and migrations/postgres/001_init.sql.
- internal/*/*_test.go, README.md, and .gitignore.

### Task 1: Initialize the repository and protocol package

**Files:** Create ../udx710-log-server/.gitignore, go.mod, internal/protocol/types.go, and internal/protocol/types_test.go.

**Interfaces:** Produce type Record, type IngestCounts, and Record.Validate() for ingest and store packages.

- [ ] **Step 1: Create the sibling directory, initialize Git with branch main, and run go mod init udx710-log-server. Add ignores for binaries, coverage, local SQLite files, and .env.**
- [ ] **Step 2: Write failing JSON round-trip and required-field tests.**

~~~go
func TestRecordRoundTrip(t *testing.T) {
    input := Record{SchemaVersion: 1, DeviceID: "SN-1", BootID: "boot-1", Sequence: 7, Type: "snapshot", Source: "system", Payload: json.RawMessage("{\"cpu_usage\":12.5}")}
    encoded, err := json.Marshal(input)
    if err != nil { t.Fatal(err) }
    var got Record
    if err := json.Unmarshal(encoded, &got); err != nil { t.Fatal(err) }
    if got.DeviceID != "SN-1" || string(got.Payload) != "{\"cpu_usage\":12.5}" { t.Fatalf("round trip mismatch: %#v", got) }
}
~~~

- [ ] **Step 3: Implement JSON tags, RFC3339 validation, schema version validation, non-empty device/boot/type/source validation, and non-negative sequence validation without a payload-size limit.**
- [ ] **Step 4: Run go test ./internal/protocol and commit.**

~~~bash
git add .
git commit -S -m "feat(protocol): 定义遥测记录协议"
~~~

### Task 2: Add policy, authentication, and database migrations

**Files:** Create internal/policy/policy.go and tests; internal/auth/auth.go and tests; internal/store/store.go, sql.go, sqlite.go, postgres.go; migrations/sqlite/001_init.sql and migrations/postgres/001_init.sql.

**Interfaces:** Define Group as {ID int64, Name string, Enabled bool}; produce Policy.Allows(recordType string, source string) bool, HashToken(token string) string, and these exact Store methods: LookupGroup(ctx context.Context, tokenHash string) (Group, error), CreateGroup(ctx context.Context, name string, tokenHash string) (int64, error), DisableGroup(ctx context.Context, name string) error, RotateGroup(ctx context.Context, name string, tokenHash string) error, UpsertDevice(ctx context.Context, groupID int64, deviceID string, bootID string, observedAt time.Time) error, InsertRecord(ctx context.Context, groupID int64, record protocol.Record, payload json.RawMessage) (bool, error), Health(ctx context.Context) error, and Close() error.

- [ ] **Step 1: Write tests for empty/all-allow policy, exact type/source filtering, deterministic 64-character lowercase SHA-256, and disabled group rejection.**
- [ ] **Step 2: Add pgx and pure-Go SQLite drivers with go get github.com/jackc/pgx/v5/stdlib modernc.org/sqlite; implement comma-separated STORE_TYPES and STORE_SOURCES parsing and crypto/sha256 hashing.**
- [ ] **Step 3: Write idempotent migrations for groups, devices, records, and schema_migrations. Add indexes for group/device/time and type/source/time plus a unique (group_id, device_id, boot_id, sequence) key. Use JSONB/TIMESTAMPTZ in PostgreSQL and TEXT/integer UTC timestamps in SQLite.**
- [ ] **Step 4: Implement SQLite with WAL, foreign keys, busy timeout, and one writer connection; implement PostgreSQL with pgx stdlib and bounded pool. Use prepared, parameterized statements and backend-specific upsert syntax.**
- [ ] **Step 5: Run go test ./internal/policy ./internal/auth ./internal/store; skip PostgreSQL only when TEST_POSTGRES_DSN is unset. Commit.**

~~~bash
git add internal/policy internal/auth internal/store migrations
git commit -S -m "feat(store): 支持分组鉴权与双数据库"
~~~

### Task 3: Implement streaming ingest and server redaction

**Files:** Create internal/ingest/handler.go, internal/ingest/redact.go, and internal/ingest/handler_test.go.

**Interfaces:** Produce NewHandler(store, policy) http.Handler serving HEAD and POST /v1/ingest.

- [ ] **Step 1: Write failing HTTP tests for valid gzip NDJSON, invalid Token before body read, invalid X-Device-ID on HEAD, and duplicate envelopes.**
- [ ] **Step 2: Authenticate exactly one Bearer value before touching r.Body; require X-Device-ID on HEAD; on POST reject a mismatching optional header.**
- [ ] **Step 3: Decode Content-Encoding gzip with gzip.NewReader and json.Decoder until io.EOF; do not use bufio.Scanner or a fixed line-size limit. Validate, redact, apply policy, upsert device, and insert accepted records incrementally.**
- [ ] **Step 4: Return received, stored, ignored, and duplicate counts; return 400 for malformed body and 500 for store failures. Treat unique-key conflicts as duplicate counts.**
- [ ] **Step 5: Test removal/replacement of password, token, authorization, cookie, secret, private_key, and SMS body fields; never echo original payloads.**
- [ ] **Step 6: Run go test ./internal/ingest and commit.**

~~~bash
git add internal/ingest
git commit -S -m "feat(ingest): 流式接收遥测记录"
~~~

### Task 4: Add health, group commands, and server entry point

**Files:** Create internal/health/handler.go and tests; cmd/group/main.go and tests; cmd/server/main.go.

**Interfaces:** Produce GET /healthz, group create/disable/rotate commands, and a server configured by DATABASE_URL, LISTEN_ADDR, STORE_TYPES, and STORE_SOURCES.

- [ ] **Step 1: Test health success/failure, missing group name rejection, and Token generation through a testable generator.**
- [ ] **Step 2: Return {"status":"ok"} only after a successful database health check; do not expose DSNs, hashes, counts, or raw errors.**
- [ ] **Step 3: Implement group create --name, group disable --name, and group rotate --name. Generate 32 random bytes with crypto/rand, encode base64url without padding, store only the hash, and print plaintext once.**
- [ ] **Step 4: Parse environment, select store by DSN scheme, run migrations, register /healthz and /v1/ingest, set ReadHeaderTimeout and IdleTimeout, and use signal.NotifyContext. Do not set a whole-body ReadTimeout.**
- [ ] **Step 5: Run go test ./cmd/... ./internal/health and commit.**

~~~bash
git add cmd internal/health
git commit -S -m "feat(server): 增加服务入口与分组命令"
~~~

### Task 5: Add SQLite/PostgreSQL integration tests and documentation

**Files:** Create internal/integration/ingest_sqlite_test.go, internal/integration/ingest_postgres_test.go, and README.md.

**Interfaces:** Produce local SQLite end-to-end verification and CI PostgreSQL verification instructions.

- [ ] **Step 1: Start a temporary SQLite store, create a group, send a gzip request containing a snapshot, log, and duplicate, and assert counts and stored rows.**
- [ ] **Step 2: Guard the PostgreSQL test with TEST_POSTGRES_DSN, run the same assertions, and clean the unique test group.**
- [ ] **Step 3: Document go test ./..., DATABASE_URL, LISTEN_ADDR, policy variables, group creation/rotation, HEAD testing, SQLite permissions, PostgreSQL TLS DSNs, reverse proxy HTTPS, and the absence of device retry queues.**
- [ ] **Step 4: Run go test ./... and go vet ./...; commit.**

~~~bash
git add internal/integration README.md
git commit -S -m "test(server): 增加双数据库端到端验证"
~~~

### Task 6: Verify the cross-repository contract

**Files:** Modify only files that fail the agreed contract checks.

- [ ] **Step 1: Build a fixture with snapshot, traffic, service, dmesg, and at records plus secret/SMS-body fields used only to verify redaction.**
- [ ] **Step 2: Start SQLite server, create a group, send gzip NDJSON with curl, run HEAD /v1/ingest, and assert stored/duplicate counts and absence of secret values in SQLite.**
- [ ] **Step 3: Run go test ./... and go vet ./... in the Go repository; run make -C tests test, node tests/frontend_api_test.mjs, ./tests/check_routes.sh, cd web && pnpm run build, and cd ../src && make in UDX710-TOOLS.**
- [ ] **Step 4: Review both git status --short --branch outputs and leave no binaries, local databases, plaintext Tokens, or untracked fixtures.**
