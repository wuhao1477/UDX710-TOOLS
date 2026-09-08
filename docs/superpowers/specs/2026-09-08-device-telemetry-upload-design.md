# 设备日志与遥测上传设计

## 目标

为 `UDX710-TOOLS` 增加可关闭的设备日志与遥测上传能力，并在同级目录创建独立 Go 项目 `udx710-log-server` 作为接收端。设备可保存自定义接收 URL 和分组 Token；Go 服务以一个 Token 表示一个设备分组，通过设备现有序列号 `device_id` 区分同组设备，并可选择 PostgreSQL 或 SQLite 保存数据。

本设计上传设备能够取得的全部非敏感参数、配置、运行状态、流量和日志。密码、Token、认证凭据及短信正文不得进入上传载荷。Go 服务端可按记录类型和来源决定是否保存。

## 仓库边界

现有仓库 `UDX710-TOOLS` 负责：

- 采集、脱敏、序列化和上传设备数据。
- 保存上传开关、完整接收 URL、分组 Token 和上传周期。
- 提供配置、状态和连接测试 API。
- 在系统设置页面提供上传配置入口。

新项目 `../udx710-log-server` 负责：

- 初始化独立 Git 仓库。
- 接收、鉴权、校验、去重和筛选上传记录。
- 管理分组及其 Token。
- 使用 PostgreSQL 或 SQLite 保存数据。
- 提供健康检查，不提供管理后台或数据展示页面。

## 设备端架构

新增独立 `telemetry` 模块，不经本机 HTTP API 反向采集数据。模块直接复用现有系统函数与数据结构，避免认证、JSON 反解析和本机网络调用。

模块包含四个职责：

1. 配置：从现有 `6677.db` 的 `config` 表读取和保存上传设置。
2. 采集：生成周期快照，接收状态事件，并读取新增日志。
3. 脱敏：在序列化前移除敏感字段和值。
4. 发送：在独立线程中形成 NDJSON gzip 分片并调用设备已有 `/usr/bin/curl`。

上传默认关闭。打开后立即发送完整设备快照和当前能够读取的历史日志，随后每 60 秒发送完整快照；周期允许配置为 10–3600 秒。状态变化与新增日志尽快进入后续分片，不等待下一次完整快照。

采集和上传不得阻塞 Mongoose/GLib/D-Bus 主循环。关闭开关后停止新的采集和分片，正在发送的分片允许完成。

### 配置

设备端配置键为：

- `telemetry_enabled`：上传开关，默认 `0`。
- `telemetry_url`：完整接收 URL。
- `telemetry_group_token`：分组 Token。
- `telemetry_interval_sec`：快照周期，默认 `60`。

Token 必须以明文形式保存在设备上才能用于鉴权，但查询 API 不返回 Token，只返回 `token_configured`。前端 Token 输入框保持空白；未填写新值时保留原 Token，显式清除时删除。

完整 URL 只允许 `http://` 或 `https://`，不自动追加路径。HTTP 可使用，但页面明确提示 Token 和数据会以明文传输。URL 不允许包含用户名、密码或已知 Token 查询参数。

### 数据采集范围

周期快照至少包含以下类别中设备能够取得的字段：

- 系统：主机名、内核、架构、版本、CPU、内存、温度、运行时间、时间和设备能力。
- 身份：序列号、IMEI、ICCID、IMSI、设备型号和启动 ID。
- 蜂窝网络：SIM 卡槽、运营商、数据连接、漫游、网络制式、频段、小区、信号和 QoS。
- 网络接口：接口名、MAC、IPv4、IPv6、掩码、链路状态、监听状态、实时收发速率及累计收发量。
- 接入设备：MAC、IP、接口、接入类型、信号、连接时间和流量。
- 功能状态及非敏感配置：Wi-Fi、APN、USB、ADB、供电、充电、流量限制、Rathole、IPv6 代理、通知、自动重启及手机壳模式。
- 程序版本与设备当前可见的其他非敏感状态。

Wi-Fi、APN 和管理密码不上传。Rathole Token、通知/Webhook Token、认证 Token、分组 Token、Cookie、Authorization、私钥、API Key 及其他凭据不上传。含凭据的 URL、请求头和请求体不得原样上传。短信正文不上传；短信配置、数量、状态、时间及非正文元数据可以上传。

### 日志来源

日志记录使用 `type=log`，按来源区分：

- `service`：现有 C 服务的标准输出和错误输出。
- `command`：通过 `run_command_argv()` 执行的命令、退出状态和输出。
- `at`：通过 `execute_at()` 或 `send_at()` 执行的 AT 命令、结果和错误。
- `system`：设备系统日志；优先使用设备提供的日志读取接口，并兼容常见日志文件。
- `dmesg`：内核日志。

`debug.h` 当前会在编译期丢弃 `printf`。实现时改为统一日志入口：上传关闭时继续低成本丢弃，上传开启时写入发送通道。上传模块自身日志不得再次进入上传通道，防止递归。

首次启用时上传当前可读取的系统日志和 `dmesg` 历史。旧版本已经丢弃的服务输出和历史 AT 响应无法恢复；新版进程启动后产生的服务输出和 AT 响应均可上传。后续通过内存游标读取新增内容，不为游标或日志建立持久化缓存。

### 脱敏

脱敏在设备端序列化之前执行，并在 Go 接收端再次执行同类检查。设备端至少执行：

- 删除名称匹配 `password`、`passwd`、`pwd`、`token`、`secret`、`authorization`、`cookie`、`api_key`、`access_key`、`private_key` 的字段。
- 将当前配置中已知的密码和 Token 值从自由文本日志中替换为 `[REDACTED]`。
- 删除自由文本中常见 Bearer、Basic、Cookie、密码赋值和 Token 参数。
- 对 URL 删除用户信息及凭据查询参数；无法安全拆分的凭据型 URL 整体替换为 `[REDACTED_URL]`。
- 在日志进入上传缓冲区前删除短信正文。

已知敏感值只用于内存匹配，不写入上传记录或诊断日志。

### 分片和失败语义

不设置上传数据总量上限，也不因总量截断日志。发送通道持续形成约 1 MiB 的 gzip 分片；分片大小是传输边界，不是数据保留上限。单条超大记录允许独占一个分片。

设备不保存断网队列。每个分片只发送一次，HTTP `2xx` 表示成功；DNS、连接、TLS、超时或非 `2xx` 响应均记入内存状态后丢弃该分片。服务重启后内存状态和日志游标重新开始。

内存状态包含：上传是否运行、最近成功时间、最近错误、成功分片数、失败分片数和最后服务端响应摘要。

## 设备管理 API 与前端

增加以下认证 API：

- `GET /api/telemetry/config`：返回开关、URL、周期和 `token_configured`。
- `POST /api/telemetry/config`：校验并保存配置，按新配置启动或停止上传。
- `GET /api/telemetry/status`：返回当前内存运行状态。
- `POST /api/telemetry/test`：使用当前或待保存配置执行不入库的鉴权测试。

系统设置页新增“日志与遥测上传”区域，包含上传开关、完整接收 URL、分组 Token 密码框、上传周期、连接测试以及当前状态。中文和英文文案同步增加。

连接测试对已配置接收 URL 发送 `HEAD` 请求，分组 Token 放入 `Authorization`，设备序列号放入 `X-Device-ID`。Go 接收端验证分组及设备编号后返回 `204`，不创建 `records` 数据。

## 上传协议

接收接口为配置 URL 指向的 `POST /v1/ingest`。请求头为：

```http
Authorization: Bearer <group-token>
Content-Type: application/x-ndjson
Content-Encoding: gzip
```

同一路径的 `HEAD` 连接测试使用 `Authorization` 和 `X-Device-ID` 请求头，不发送请求体。

每行是一条独立记录：

```json
{
  "schema_version": 1,
  "device_id": "device-serial",
  "boot_id": "linux-boot-id",
  "sequence": 123,
  "observed_at": "2026-09-08T10:00:00Z",
  "type": "snapshot",
  "source": "system",
  "payload": {}
}
```

字段语义：

- `schema_version`：协议版本，首版固定为 `1`。
- `device_id`：设备现有序列号；为空或无效时设备端不上传，服务端拒绝。
- `boot_id`：读取 Linux boot ID，区分设备重启。
- `sequence`：当前进程内单调递增序号。
- `observed_at`：设备实际采集时间，UTC RFC 3339。
- `type`：`snapshot`、`event` 或 `log`。
- `source`：记录来源，例如 `traffic`、`network`、`service`、`system`、`dmesg`、`at`。
- `payload`：已经脱敏的 JSON 对象。

唯一键为 `(group_id, device_id, boot_id, sequence)`。Go 服务以流式方式解压并逐行解析，不把完整请求读入内存，也不设置请求体总量限制。成功响应返回：

```json
{"received":10,"stored":8,"ignored":1,"duplicate":1}
```

状态码：

- `200`：请求处理完成。
- `204`：`HEAD` 鉴权测试成功。
- `400`：协议、设备编号、压缩或 NDJSON 无效。
- `401`：Token 不存在、无效或分组已停用。
- `500`：数据库操作失败。

## Go 服务架构

新仓库使用 Go 标准库 `net/http`、`encoding/json`、`compress/gzip` 和 `database/sql`，不引入 Web 框架或 ORM。PostgreSQL 使用 pgx 的 `database/sql` 驱动；SQLite 使用无需 CGO 的驱动，保持部署简单。

目录结构：

```text
udx710-log-server/
├── cmd/server/
├── cmd/group/
├── internal/ingest/
├── internal/store/
├── migrations/
├── go.mod
└── README.md
```

配置：

- `DATABASE_URL`：`sqlite://...` 或 `postgres://...`，按 scheme 选择数据库。
- `LISTEN_ADDR`：监听地址，默认 `:8080`。
- `STORE_TYPES`：允许保存的记录类型，默认全部。
- `STORE_SOURCES`：允许保存的记录来源，默认全部。

服务启动时执行内嵌迁移。接收流程为：验证 Token → 流式解析 → 服务端脱敏检查 → 应用保存策略 → 登记设备 → 插入记录 → 返回计数。通过鉴权但被策略忽略的记录不写入 `records`，仍更新设备最后在线时间。

服务不配置自动保留期，不自动删除记录。需要减少存储时通过 `STORE_TYPES` 或 `STORE_SOURCES` 停止保存对应数据。

## Token 与分组

一个 Token 对应一个分组，一个分组允许任意数量设备使用同一 Token。Token 必须先由 Go 服务端命令创建，未知 Token 返回 `401`。

`cmd/group` 支持：

- 创建分组并生成 Token。
- 停用分组。
- 轮换 Token，旧 Token 立即失效。

Token 使用密码学安全随机数生成。数据库只保存 Token 的 SHA-256 哈希；Token 具有足够熵，不使用面向低熵密码的慢哈希。创建或轮换时只输出一次明文 Token。

## 数据库模型

核心表：

### `groups`

- `id`
- `name`
- `token_hash`
- `enabled`
- `created_at`
- `updated_at`

### `devices`

- `group_id`
- `device_id`
- `first_seen_at`
- `last_seen_at`
- `last_boot_id`
- `last_observed_at`

主键为 `(group_id, device_id)`。

### `records`

- `id`
- `group_id`
- `device_id`
- `boot_id`
- `sequence`
- `observed_at`
- `received_at`
- `type`
- `source`
- `payload`

PostgreSQL 的 `payload` 使用 `JSONB`，SQLite 使用 JSON 文本。两种数据库都为 `(group_id, device_id, observed_at)`、`(type, source, observed_at)` 建立索引，并为去重键建立唯一约束。

所有 SQL 使用参数化语句。SQLite 开启 WAL、外键和 busy timeout，并限制为一个写连接；PostgreSQL 使用连接池。

## 错误处理

- 设备配置无有效序列号、URL 或 Token 时不启动上传，并在状态 API 返回原因。
- 上传线程与主服务生命周期绑定，退出时停止采集、结束当前分片并回收子进程。
- 上传模块执行 curl 时使用参数数组，不拼接 Shell 命令。
- Go 服务在读取请求体之前完成 Token 鉴权。
- NDJSON 中无效记录计入错误并使当前请求返回 `400`；已提交记录依赖唯一键保持幂等。
- 上传模块和接收服务记录错误时不得输出 Token、Authorization 头或未脱敏请求体。
- Go 服务提供 `GET /healthz`，数据库不可用时返回非 `2xx`。

## 验证

设备端验证：

- 配置默认值、保存、Token 不回显、URL 和周期校验。
- 结构化字段、URL、请求头、自由文本及短信正文脱敏。
- 协议信封、序号、NDJSON 和 gzip 分片。
- 上传成功、鉴权失败、网络失败、关闭开关和无序列号路径。
- 日志初始读取、增量游标以及上传日志防递归。
- 现有 C 测试、前端 API 路由检查、前端构建和 aarch64 交叉编译。

Go 服务验证：

- 分组创建、停用、轮换及 Token 哈希校验。
- gzip NDJSON 流式接收、协议校验、服务端脱敏、策略忽略和去重。
- SQLite 迁移与读写。
- PostgreSQL 迁移与读写，由 CI 数据库服务运行。
- 使用 SQLite 完成一次设备格式的端到端上传及 `HEAD` 连接测试。

## 验收标准

1. 上传默认关闭；重启后保留开关、URL、Token 和周期。
2. 同一分组 Token 可接收多个不同序列号设备，并能按分组、设备和时间查询。
3. 每 60 秒上传完整快照，配置周期可在 10–3600 秒内修改；状态事件与日志单独上传。
4. 首次启用上传当前可读取的系统日志和 `dmesg` 历史，之后上传新增日志；数据总量不被截断。
5. 密码、其他 Token、认证凭据和短信正文不会出现在上传载荷或数据库记录中；分组 Token 只出现在 `Authorization` 请求头。
6. 设备断网时不写本地缓存，失败分片直接丢弃且不影响主服务。
7. Go 服务可在不改业务代码的情况下通过 `DATABASE_URL` 切换 PostgreSQL 与 SQLite。
8. Go 服务可按记录类型和来源决定是否保存，默认保存全部允许数据。
9. Go 服务不需要管理后台即可完成启动、健康检查和分组 Token 管理。

## 非目标

- 不实现设备端断网队列、补传或持久化日志游标。
- 不实现 Go 服务管理后台、图表、告警、搜索或导出界面。
- 不上传任何短信正文、密码、Token 或认证凭据。
- 不修改现有业务接口兼容性，也不重构与上传无关的既有模块。
