# 通知管理 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不增加 SMTP 和常驻服务的前提下，把现有短信 Webhook 提升为可复用的通知管理模块，并接入设备事件规则。

**Architecture:** 新增 `notification` 后端模块管理 Webhook 配置、事件规则、单生产者队列、发送线程和日志；短信、认证、网络、客户端、内置卡和流量模块只发布标准事件。保留旧短信 Webhook 路由作为同一处理器的入口，新前端通过通知管理路由访问。

**Tech Stack:** C11、pthread、SQLite 配置表、现有 `/usr/bin/curl`、Mongoose、Vue 3、Vite、Tailwind CSS。

**Spec:** `docs/superpowers/specs/2026-09-07-notification-management-design.md`

## Global Constraints

- 不增加 SMTP、OpenSSL、邮件库或新的常驻进程。
- 复用现有 `webhook_config`、`netif_get_list()`、`wifi_get_clients()`、vnstat 和设备数据源。
- 通知队列最多 8 条；每个通知项默认冷却 300 秒；通知列表为空时不触发事件。
- 当前设备没有可用 RJ45 Linux 接口时不得生成 RJ45 事件。
- URL、请求体和请求头不得拼接进 shell 命令。
- 每个生产代码改动先有失败测试，再实现最小代码。

### Task 1: 规则和模板纯函数

**Files:**
- Create: `src/include/system/notification.h`
- Create: `src/system/notification_rules.c`
- Test: `tests/notification_test.c`
- Modify: `tests/Makefile`

**Interfaces:**
- `notification_event_id()`、`notification_event_from_id()`、`notification_rule_validate()`。
- `notification_expand_template()` 替换通用模板变量。

- [ ] 写失败测试：事件 ID 映射、通知项校验、短信和通用模板替换。
- [ ] 运行 `make -C tests clean notification_test`，确认因文件和符号不存在而失败。
- [ ] 实现最小纯函数和固定事件表。
- [ ] 运行 `./tests/notification_test`，确认通过。

### Task 2: 通知模块和安全 Webhook 发送

**Files:**
- Create: `src/system/notification.c`
- Modify: `src/include/system/notification.h`
- Modify: `src/include/system/exec_utils.h`
- Modify: `src/system/exec_utils.c`
- Modify: `src/Makefile`

**Interfaces:**
- `notification_init()`、`notification_deinit()`、`notification_emit()`、`notification_test_webhook()`。
- `notification_get_webhook_config()`、`notification_save_webhook_config()`、`notification_get_logs()`。
- `run_command_argv()` 使用 `execvp` 参数数组执行命令。

- [ ] 写失败编译检查，要求 Makefile 能识别 `notification.o` 和 `run_command_argv()`。
- [ ] 实现固定长度队列、单发送线程、冷却时间和 Webhook 日志。
- [ ] 用参数数组调用 curl，保留现有模板变量和平台配置。
- [ ] 运行主机侧编译检查；确认通知模块未使用 `system()`、`popen()` 或 shell 拼接 URL。

### Task 3: 数据库、API 和旧路由兼容

**Files:**
- Modify: `src/system/database.c`
- Modify: `src/system/security.c`
- Modify: `src/include/handlers/handlers.h`
- Modify: `src/handlers/handlers.c`
- Modify: `src/handlers/http_server.c`

**Interfaces:**
- `GET/POST /api/notifications/webhook`。
- `POST /api/notifications/webhook/test`。
- `GET /api/notifications/logs`。
- `GET/POST /api/notifications/rules`、`PUT/DELETE /api/notifications/rules/:id`。
- 原 `/api/sms/webhook*` 路由继续调用同一通知处理器。

- [ ] 写失败路由检查，要求新增路由和规则表名称存在。
- [ ] 增加 `notification_entries` 表、旧规则一次性迁移及出厂重置清理。
- [ ] 实现配置、规则、测试和日志处理器，并限制事件 ID、阈值单位、冷却时间和请求体长度。
- [ ] 在服务启动/停止时初始化和释放通知模块。
- [ ] 运行 `tests/check_routes.sh` 和通知 API 静态检查。

### Task 4: 接入事件源

**Files:**
- Modify: `src/system/sms.c`
- Modify: `src/handlers/handlers.c`
- Modify: `src/system/netif.c`
- Modify: `src/handlers/http_server.c`
- Modify: `src/system/builtin_cards.c`
- Modify: `src/system/traffic.c`
- Modify: `src/include/system/traffic.h`
- Modify: `src/system/sysinfo.c`

**Interfaces:**
- 短信、登录、运营商切换由动作成功点发布事件。
- `notification_maintenance()` 每 30 秒执行一次按需快照。
- 快照比较网络接口/IP、接入客户端、网络类型、信号、流量和数据网络状态。

- [ ] 写失败测试，覆盖 MAC、接口/IP、4G/5G 和阈值状态变化只在越过状态时触发。
- [ ] 从 `sms.c` 移除 Webhook 发送实现，改为发布短信事件。
- [ ] 在登录和成功切卡路径发布事件。
- [ ] 实现按规则启用的轮询快照；RJ45 只匹配 `eth*`、`en*`、`lan*`。
- [ ] 在主循环维护点调用通知维护，并确认关闭规则时不执行对应扫描。

### Task 5: 前端通知管理

**Files:**
- Create: `web/src/components/NotificationManager.vue`
- Modify: `web/src/components/SmsManager.vue`
- Modify: `web/src/App.vue`
- Modify: `web/src/i18n/locales/zh-CN.js`
- Modify: `web/src/i18n/locales/en-US.js`
- Test: `tests/frontend_api_test.mjs`

**Interfaces:**
- “通知管理”独立菜单。
- Webhook 页面保留平台模板、启用开关、URL、请求体、请求头、教程、测试和日志。
- 规则页面显示事件名称、启用开关、阈值、单位和冷却时间。

- [ ] 写失败前端路由/文案检查，确认通知菜单存在且短信页签不再有转发页签。
- [ ] 搬迁 Webhook UI 和教程，不复制发送逻辑。
- [ ] 增加规则保存、刷新、测试和日志加载。
- [ ] 运行 `pnpm run build` 和 `node tests/frontend_api_test.mjs`。

### Task 6: 集成验证

**Files:**
- Modify: `tests/Makefile` if required by actual test linkage.
- Modify: `tests/check_routes.sh` if required by actual route list.

- [ ] 运行 `make -C tests clean test`。
- [ ] 运行 `cd web && pnpm run build`。
- [ ] 运行 `sh tests/check_routes.sh`。
- [ ] 使用 CI 同款 aarch64 Makefile 做后端交叉编译检查；工具链缺失时记录具体缺失项，不宣称通过。
- [ ] 查看 `git diff --stat` 和 `git status --short`，确认没有 SMTP、OpenSSL 或部署改动。
