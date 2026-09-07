# SZ50 当前设备功能合并与适配 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 fork 的 `sz50` 分支中保留 `main` 的通用功能，移植 SZ50 专用功能，并适配当前无电池、外部供电、带 Type-C 与 RJ45 形态的 UDX710 `marlin3e` 设备。

**Architecture:** 以 fork `main` 为代码基线，不合并两个无共同历史的分支。新增轻量 `device_profile` 模块集中探测 WiFi、LED、电源、Type-C、RJ45、USB gadget 和蜂窝数据接口；SZ50 的硬件模块通过该模块取得运行时路径，通用版的认证和业务模块保持不变。前端通过能力接口隐藏不适用的电池控制，并展示 Type-C/RJ45 的实际状态。

**Tech Stack:** C11、GLib/GIO/D-Bus、Mongoose、Vue 3、Vite、Tailwind CSS、BusyBox shell、aarch64-linux-gnu GCC。

**Spec:** `docs/superpowers/specs/2026-09-07-sz50-current-device-design.md`

## Global Constraints

- 目标分支固定为 fork 的小写 `sz50`。
- `main` 的认证中间件、APN、插件、脚本、Rathole、IPv6 代理、网络接口和密保功能必须保留。
- 当前设备没有内置电池；充电 API 必须返回不可用状态，不得写入不存在的电池节点。
- RJ45 物理接口存在，但没有 Linux 有线接口或 `/dev/seth_lte` 时不得伪造可用状态。
- Type-C 当前 gadget 为 `0x2dee:0x4d51`，RNDIS/ADB/串口状态可读；开发验证不得切换 USB gadget。
- 不把设备序列号、IMEI、ICCID、密码、MAC 或其他实例数据写入源码和测试夹具。
- Python 命令必须在 Conda 环境中执行；本计划使用 C、Shell、Node 工具，不依赖 Python。
- 提交信息使用 `<type>(scope): <中文动词开头摘要>`，提交默认启用 GPG 签名。

---

### Task 1: 建立可测试的设备能力探测模块

**Files:**
- Create: `tests/device_profile_test.c`
- Create: `tests/Makefile`
- Create: `src/include/system/device_profile.h`
- Create: `src/system/device_profile.c`
- Modify: `src/Makefile`

**Interfaces:**
- Produces `DeviceProfile`, `device_profile_init()`, `device_profile_refresh()`, `device_profile_get()`。
- Produces pure selectors `device_profile_parse_hostapd()`, `device_profile_select_led()`, `device_profile_select_data_iface()` 和 `device_profile_classify_usb()`，供主机测试调用。

- [ ] **Step 1: Write the failing test**

在 `tests/device_profile_test.c` 中先写以下行为测试；测试只调用纯选择函数，不访问真实设备：

```c
#include <assert.h>
#include <string.h>
#include "device_profile.h"

static void test_hostapd_wlan1(void) {
    char iface[32], config[256];
    assert(device_profile_parse_hostapd(
        "/usr/sbin/hostapd -s -B /mnt/data/hostapd_wlan1.conf -i wlan1",
        iface, sizeof(iface), config, sizeof(config)) == 0);
    assert(strcmp(iface, "wlan1") == 0);
    assert(strcmp(config, "/mnt/data/hostapd_wlan1.conf") == 0);
}

static void test_led_fallback(void) {
    const char *names[] = {"red_led", "green_led", "blue_led", "sc27xx:red"};
    char path[64];
    assert(device_profile_select_led(names, 4, "red", path, sizeof(path)) == 0);
    assert(strcmp(path, "red_led") == 0);
}

static void test_data_interface_priority(void) {
    const char *names[] = {"wlan1", "usb0", "sipa_eth0"};
    char iface[32];
    assert(device_profile_select_data_iface(names, 3, iface, sizeof(iface)) == 0);
    assert(strcmp(iface, "sipa_eth0") == 0);
}

static void test_current_typec_rndis(void) {
    const char *functions[] = {"rndis.gs4", "gser.gs2", "ffs.adb"};
    assert(device_profile_classify_usb("0x2dee", "0x4d51", functions, 3) ==
           DEVICE_USB_RNDIS_CURRENT);
}

static void test_unsupported_typec_switch(void) {
    const char *functions[] = {"rndis.gs4", "gser.gs2", "ffs.adb"};
    assert(device_profile_classify_usb("0x2dee", "0x4d51", functions, 3) !=
           DEVICE_USB_SWITCHABLE_GENERIC);
}

int main(void) {
    test_hostapd_wlan1();
    test_led_fallback();
    test_data_interface_priority();
    test_current_typec_rndis();
    test_unsupported_typec_switch();
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make -C tests clean test`

Expected: FAIL during compilation because `device_profile.h`, the selectors, and the USB enum do not exist yet.

- [ ] **Step 3: Write minimal implementation**

Define the public API in `src/include/system/device_profile.h`:

```c
typedef enum {
    DEVICE_USB_UNKNOWN = 0,
    DEVICE_USB_RNDIS_CURRENT = 1,
    DEVICE_USB_SWITCHABLE_GENERIC = 2
} DeviceUsbMode;

typedef struct {
    int mains_powered;
    int battery_supported;
    int rj45_physical_present;
    int rj45_interface_present;
    int rj45_usable;
    int typec_present;
    int typec_host_capable;
    int usb_mode_switch_supported;
    int usb_rndis_available;
    char wifi_iface[32];
    char wifi_config[256];
    char data_iface[32];
    char rj45_iface[32];
    char led_red[256];
    char led_green[256];
    char led_blue[256];
    char usb_vid[16];
    char usb_pid[16];
    char reason_rj45[160];
    char reason_power[160];
    char reason_usb[160];
} DeviceProfile;

int device_profile_init(void);
int device_profile_refresh(void);
const DeviceProfile *device_profile_get(void);
int device_profile_parse_hostapd(const char *cmdline, char *iface,
                                 size_t iface_size, char *config,
                                 size_t config_size);
int device_profile_select_led(const char *const *names, size_t count,
                              const char *color, char *out, size_t out_size);
int device_profile_select_data_iface(const char *const *names, size_t count,
                                     char *out, size_t out_size);
DeviceUsbMode device_profile_classify_usb(const char *vid, const char *pid,
                                          const char *const *functions,
                                          size_t count);
```

Implement filesystem detection with fixed system roots only: `/sys/class/leds`, `/sys/class/power_supply`, `/sys/class/net`, `/sys/kernel/config/usb_gadget/g1`, `/sys/class/typec`, `/etc/modem/modem.ini`, and the running hostapd command line. Mark the current board as externally powered when no battery exists; mark RJ45 physical presence when `ro.vendor.modem.eth=seth_lte` is present, while setting `rj45_usable` only when `eth0`/`en*`/`lan*` or `/dev/seth_lte` is actually available. Read current VID/PID and linked gadget functions without writing them.

Add `device_profile.o` to the backend object list and include `-Isystem` compatibility through the existing `src/include` layout.

- [ ] **Step 4: Run test to verify it passes**

Run: `make -C tests clean test`

Expected: the five assertions pass with exit code 0.

- [ ] **Step 5: Commit**

```bash
git add tests src/include/system/device_profile.h src/system/device_profile.c src/Makefile
git commit -S -m "feat(device): 增加设备能力探测"
```

### Task 2: 移植 SZ50 硬件模块并改为运行时路径

**Files:**
- Create: `src/include/system/led.h`
- Create: `src/include/system/power_key.h`
- Create: `src/include/system/wifi.h`
- Create: `src/include/system/factory_reset.h`
- Create: `src/system/led.c`
- Create: `src/system/power_key.c`
- Create: `src/system/wifi.c`
- Create: `src/system/factory_reset.c`
- Modify: `src/Makefile`
- Modify: `src/main.c`

**Interfaces:**
- Consumes `DeviceProfile` from Task 1.
- Produces the upstream SZ50 APIs: `led_init/led_deinit`, `power_key_init/power_key_deinit`, `wifi_init` and WiFi client/ACL handlers, and `handle_factory_reset`.

- [ ] **Step 1: Write the failing integration check**

Create `tests/check_sz50_symbols.sh` that fails unless the four module headers declare the required entry points and the source files reference `device_profile_get()` instead of fixed `wlan0`, `lte_red`, `nr_green`, or `battery` paths. Run it before porting:

```sh
#!/bin/sh
set -eu
grep -q 'device_profile_get' src/system/led.c
grep -q 'device_profile_get' src/system/wifi.c
grep -q 'device_profile_get' src/system/factory_reset.c
! grep -q 'WLAN_IFACE.*wlan0' src/system/wifi.c
! grep -q '/sys/class/leds/lte_' src/system/led.c
```

Run: `sh tests/check_sz50_symbols.sh`

Expected: FAIL because the modules are not present in the main baseline.

- [ ] **Step 2: Port the minimal module code**

Copy the SZ50 module behavior into the main layout, then make these concrete changes:

- LED initialization maps `red`, `green`, and `blue` through the profile paths; absent `sc27xx:*` nodes are ignored, and unavailable colors make the API return a stable unsupported result.
- `power_key` keeps `/dev/input/event0` as the first candidate, checks existence before starting its listener, and calls the profile-backed LED functions.
- WiFi uses the detected hostapd interface/config; current `wlan1` and `/mnt/data/hostapd_wlan1.conf` must work without changing the generic fallback for a different hostapd instance.
- Factory reset resets only detected user configuration files; it must not assume `hostapd_2g.conf` or `hostapd_5g.conf` exists.
- Every HTTP handler uses the main branch `HTTP_CHECK_*`, `HTTP_OK`, `HTTP_ERROR`, and JSON builder helpers rather than the old SZ50 response helpers.

Update `main.c` to call `device_profile_init()`, `led_init()`, and `power_key_init()` before `http_server_start()`, and to deinitialize them on every exit path.

- [ ] **Step 3: Run the integration check**

Run: `sh tests/check_sz50_symbols.sh`

Expected: PASS with no output and exit code 0.

- [ ] **Step 4: Commit**

```bash
git add src/include/system src/system src/Makefile src/main.c tests/check_sz50_symbols.sh
git commit -S -m "feat(sz50): 移植硬件控制模块"
```

### Task 3: 合并路由并实现电源、RJ45、Type-C 能力接口

**Files:**
- Modify: `src/include/handlers/handlers.h`
- Modify: `src/handlers/handlers.c`
- Modify: `src/handlers/http_server.c`
- Modify: `src/system/charge.c`
- Modify: `src/system/sysinfo.c`
- Modify: `src/system/traffic.c`
- Modify: `src/system/usb_mode.c`
- Modify: `src/include/system/usb_mode.h`
- Create: `tools/rj45_bridge_watch.sh`

**Interfaces:**
- Consumes `DeviceProfile` and all main branch APIs.
- Produces `/api/capabilities`, `/api/led/*`, `/api/wifi/*`, `/api/factory-reset`, and safe status behavior for `/api/charge/*`, `/api/usb/*`.

- [ ] **Step 1: Write the route and capability checks**

Create `tests/check_routes.sh` with exact assertions for both route sets:

```sh
#!/bin/sh
set -eu
for route in /api/auth/login /api/apn/config /api/plugins /api/rathole/status /api/ipv6-proxy/status /api/netif/list /api/usb/mode; do
  grep -q "\\\"$route\\\"" src/handlers/http_server.c
done
for route in /api/capabilities /api/led/status /api/led/control /api/wifi/status /api/wifi/config /api/wifi/clients /api/factory-reset; do
  grep -q "\\\"$route\\\"" src/handlers/http_server.c
done
```

Run: `sh tests/check_routes.sh`

Expected: FAIL because the SZ50 routes and capabilities route are not yet in the main baseline.

- [ ] **Step 2: Add route integration and capability response**

Add handler declarations and route branches for the union of main and SZ50 endpoints. Put the capabilities route behind the existing GET whitelist only when it contains read-only status; all LED, WiFi, reset, USB and network control routes remain Token-protected.

Implement `handle_capabilities()` in `handlers.c` using the profile fields. Its JSON response must include:

```json
{
  "power": {"source": "external", "battery_supported": false},
  "wifi": {"interface": "wlan1", "config": "/mnt/data/hostapd_wlan1.conf"},
  "cellular": {"interface": "sipa_eth0"},
  "rj45": {"physical_present": true, "interface_present": false, "usable": false},
  "typec": {"present": true, "gadget_vid": "0x2dee", "gadget_pid": "0x4d51", "rndis": true},
  "led": {"red": true, "green": true, "blue": true}
}
```

The values above are response shape examples; runtime identifiers come from detection and must not include instance secrets.

- [ ] **Step 3: Make unsupported hardware safe**

- `charge.c`: do not create the uevent monitor when `/sys/class/power_supply` has no battery; GET returns `battery_supported:false` and `power_source:"external"`, POST and manual charge endpoints return HTTP 409 with a clear unsupported reason.
- `sysinfo.c`: add `battery_supported` and `power_source` fields while retaining existing JSON keys with neutral values for compatibility; use device-tree/model fallback when `/home/cpuinfo` is absent.
- `traffic.c`: replace the fixed interface with the profile-selected `sipa_eth0`/cellular fallback and skip vnstat initialization when no selected interface exists.
- `usb_mode.c`: accept the current `0x2dee:0x4d51` plus `rndis.gs4` as read-only RNDIS status; refuse generic hot switching unless the requested function directory and gadget configuration are both present. Never default an unknown gadget to RNDIS.
- Add `tools/rj45_bridge_watch.sh` that checks `eth0`, `en*`, and `lan*`, excludes `usb0`/`sipa_*`, and adds only an actually present interface to the existing `tether` bridge. No-op when the physical RJ45 has no software path.

- [ ] **Step 4: Run the checks**

Run: `sh tests/check_routes.sh && sh tests/check_sz50_symbols.sh`

Expected: both scripts exit 0.

- [ ] **Step 5: Commit**

```bash
git add src tools tests/check_routes.sh
git commit -S -m "feat(device): 适配电源有线网和Type-C"
```

### Task 4: 接入 SZ50 前端功能与设备能力显示

**Files:**
- Create: `web/src/components/WifiManager.vue`
- Create: `web/src/components/DeviceManager.vue`
- Modify: `web/src/App.vue`
- Modify: `web/src/composables/useApi.js`
- Modify: `web/src/components/BatteryManager.vue`
- Modify: `web/src/components/UsbMode.vue`
- Modify: `web/src/i18n/locales/zh-CN.js`
- Modify: `web/src/i18n/locales/en-US.js`

**Interfaces:**
- Consumes `/api/capabilities`, `/api/wifi/*`, `/api/led/*`, `/api/factory-reset` and the existing main APIs.
- Produces two menu entries for WiFi and device access management, with capability-aware battery, Type-C and RJ45 UI states.

- [ ] **Step 1: Add API wrappers and failing build check**

Add these functions to `useApi.js` before importing the new components:

```js
export async function getCapabilities() {
  return request('/api/capabilities')
}

export async function getWifiStatus() {
  return request('/api/wifi/status')
}

export async function setWifiConfig(config) {
  return request('/api/wifi/config', { method: 'POST', body: JSON.stringify(config) })
}
```

Run: `pnpm --dir web run build`

Expected: FAIL because the SZ50 component imports and referenced API functions are not yet present.

- [ ] **Step 2: Port and connect components**

Port `WifiManager.vue` and `DeviceManager.vue` from `upstream/SZ50`, then add them to `App.vue` with `wifi` and `devices` menu entries. Keep all main components and auth flow. Load capabilities after successful authentication and pass the result through `provide()` so:

- Battery menu and controls show “external power / no battery” when `battery_supported` is false.
- USB page shows current `0x2dee:0x4d51` RNDIS status and disables NCM/ECM switching when `usb_mode_switch_supported` is false.
- Device manager displays RJ45 physical-present/system-unavailable distinctly from a working interface.
- WiFi settings display the detected interface rather than assuming `wlan0`.

- [ ] **Step 3: Run the frontend build**

Run: `pnpm --dir web install --lockfile=false --ignore-scripts` and then `pnpm --dir web run build`

Expected: Vite exits 0 and produces `web/dist` without unresolved imports.

- [ ] **Step 4: Commit**

```bash
git add web
git commit -S -m "feat(web): 接入SZ50设备管理"
```

### Task 5: Update packaging, CI and documentation

**Files:**
- Modify: `.github/workflows/build.yml`
- Modify: `README.md`
- Modify: `README_CN.md`
- Modify: `src/README.md`
- Create: `tools/start.sh`
- Create: `tools/device-capabilities.md`

**Interfaces:**
- Consumes the built server, `web/dist`, and `tools/rj45_bridge_watch.sh`.
- Produces a package that starts the server with the device's `start-stop-daemon`, starts the RJ45 watcher as a separate no-op-safe process, and documents current device limits.

- [ ] **Step 1: Write packaging assertions**

Extend `tests/check_routes.sh` to require the CI package contains `server`, `start.sh`, and `rj45_bridge_watch.sh` after the packaging commands run.

- [ ] **Step 2: Update packaging**

Change the workflow package step to copy the built server, `web/dist`, `tools/start.sh`, and `tools/rj45_bridge_watch.sh` into `output/6677`. `tools/start.sh` must use `start-stop-daemon` when present and return a clear message if the device lacks it; it must not invoke USB switching or write rootfs files.

- [ ] **Step 3: Document device differences**

Document external power/no battery, current Type-C gadget composition, RJ45 physical-versus-software state, `wlan1`, `sipa_eth0`, detected LED names, and the fact that current firmware cannot make the missing RJ45 modem path available by user-space configuration alone.

- [ ] **Step 4: Run shell checks**

Run: `/bin/sh -n tools/start.sh tools/rj45_bridge_watch.sh tests/check_routes.sh tests/check_sz50_symbols.sh`

Expected: all scripts pass syntax validation.

- [ ] **Step 5: Commit**

```bash
git add .github README.md README_CN.md src/README.md tools
git commit -S -m "chore(build): 完善设备版打包说明"
```

### Task 6: Full verification and push

**Files:**
- Modify: only files required by verification fixes.

- [ ] **Step 1: Review the plan and diff**

Run: `git diff --check && git status --short && git diff --stat origin/main...HEAD`

Expected: no whitespace errors; only planned files changed.

- [ ] **Step 2: Run host tests and static checks**

Run: `make -C tests clean test && /bin/sh -n tools/*.sh tests/*.sh && sh tests/check_routes.sh && sh tests/check_sz50_symbols.sh`

Expected: all commands exit 0.

- [ ] **Step 3: Build the frontend**

Run: `pnpm --dir web run build`

Expected: Vite exits 0.

- [ ] **Step 4: Run the available backend build**

Run: `make -C src`

Expected: exit 0 when the configured aarch64 toolchain is present. If the toolchain is unavailable, record the exact missing command and rely on the CI workflow build for cross-compilation verification; do not claim a local backend build passed.

- [ ] **Step 5: Inspect final branch and push**

Run: `git status --short --branch`, `git log --oneline --decorate -8`, then `git push -u origin sz50`.

Expected: origin contains `refs/heads/sz50`; record the pushed commit and verification results.
