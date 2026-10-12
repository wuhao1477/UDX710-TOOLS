# 内置卡实名校验与运营商切换回主线方案

## 目标

将 `origin/sz50` 中已经验证过的内置卡运营商能力收回当前 `main`，只合并以下范围：

- 读取设备内置卡配置并脱敏展示 ICCID；
- 查询内置卡实名状态；
- 实名状态为 `2` 时，调用设备本地 `127.0.0.1:8443` 的原生 goform 切换运营商；
- 在现有网络管理页面中提供操作入口。

不合并 SZ50 分支的 Wi-Fi、ADB、LED、电源、设备接入、遥测和其他硬件适配功能。

## 设备能力门控

只有 `getDevInfo` 同时满足以下条件才启用功能：

| 字段 | 允许值 |
|---|---|
| `oemname` | `SZ` |
| `devtype` | `541` 或 `542` |
| `devsubtype` | `SRB876` |

不满足时，接口返回 HTTP 409，前端显示不支持，不执行任何本地设备写操作。

## 请求链路

```text
浏览器
  -> /api/builtin-cards/switch {operatorId}
  -> 主服务读取 getDevInfo / iccidcfg
  -> 外部实名接口查询 realNameStatus
  -> realNameStatus == 2
  -> 主服务访问 http://127.0.0.1:8443
  -> setPriorityMnc
```

浏览器不直接访问设备的 8443 端口。`/api/goform/proxy` 只允许固定的读取操作；切换操作也只接受 `operatorId`，由后端重新执行完整实名校验，拒绝外部传入裸 `priorityMnc`。

## 运营商映射

| 运营商 | `operatorId` | `priorityMnc` |
|---|---:|---:|
| 中国移动 | `46000` | `7` |
| 中国电信 | `46003` / `46011` | `9` |
| 中国联通 | `46001` | `11` |

切换请求使用：

```text
goformId=setDeviceConfig
configOption=setPriorityMnc
priorityMnc=<mapped value>
configValue=<mapped value>
save=1
```

只有设备返回 `result == 0` 才向前端报告成功。

## 模块划分

- `src/system/builtin_card_rules.*`：设备型号门控、运营商映射、实名响应解析和 goform 参数构造；
- `src/system/goform_client.*`：访问设备本地 HTTP 服务和实名查询接口；
- `src/system/builtin_cards.*`：内置卡读取、HTTP API、实名校验和切换编排；
- `src/system/goform_proxy.*`：受限的本地 goform 读取/切换代理；
- `web/src/components/BuiltinCardManager.vue`：网络管理页的内置卡操作界面。

## API

新增接口均沿用现有 Token 认证中间件：

- `GET /api/builtin-cards`：读取当前运营商和内置卡列表；
- `POST /api/builtin-cards/realname`：请求实名状态查询，参数 `{operatorId}`；
- `POST /api/builtin-cards/switch`：实名校验并执行切换，参数 `{operatorId}`；
- `POST /api/goform/proxy`：固定白名单的设备本地 goform 代理。

ICCID 只返回前六位和后四位，中间使用 `****` 脱敏；代码和测试不写入真实设备实例数据。

## 验证

```sh
make -C tests test
sh tests/check_routes.sh
node tests/frontend_api_test.mjs
cd web && npm run build
```

后端交叉编译仍需在具备 `aarch64-linux-gnu-gcc` 和目标 GLib 运行库的 CI/设备构建环境中执行。
