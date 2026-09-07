# 当前设备适配说明

本分支面向外部供电的 UDX710 `marlin3e` 路由器形态设备。

## 已识别硬件

- 无内置电池，`/sys/class/power_supply` 为空；页面显示外部供电，充电控制返回不适用。
- WiFi 运行时从 hostapd 命令行识别接口；当前设备为 `wlan1`，配置为 `/mnt/data/hostapd_wlan1.conf`。
- 蜂窝数据接口优先使用 `sipa_eth0`。
- LED 使用 `red_led`、`green_led`、`blue_led`，同时兼容 `sc27xx:red/green/blue`。
- Type-C 当前 gadget 为 `0x2dee:0x4d51`，组合包含 RNDIS、ADB 和串口功能；本分支只读取状态，不对该组合做通用热切换。
- RJ45 物理接口存在，但当前固件没有 `eth0`、`en*`、`lan*` 或 `/dev/seth_lte` 数据路径，因此能力接口会显示“物理存在、系统侧不可用”。

## RJ45 与 Type-C 外接网卡

`rj45_bridge_watch.sh` 会周期性查找 `eth*`、`en*`、`lan*` 接口，并在出现后加入 `tether` 网桥。当前没有这些接口时脚本不执行任何操作。

Type-C 主机模式接入 RTL8152/RTL8153 外置网卡后，驱动注册出有线接口即可被 watcher 识别；`usb0`、`sipa_*` 和 WiFi 接口会被排除，避免把现有 USB gadget 或蜂窝接口加入 RJ45 网桥。

## 内置卡运营商

仅当 goform `getDevInfo` 返回 `oemname=SZ`、`devtype=541/542`、`devsubtype=SRB876` 时显示内置卡运营商管理。

当前已确认的 `operatorId` 到设备内部 `priorityMnc` 映射：

| 运营商 | operatorId | priorityMnc |
|---|---:|---:|
| 中国移动 | 46000 | 7 |
| 中国电信 | 46003/46011 | 9 |
| 中国联通 | 46001 | 11 |

切换前查询实名接口，只有 `realNameStatus=2` 才允许前端调用设备原生 `setPriorityMnc` goform。切换请求只使用设备现有参数，不写入 `iccidcfg`、IMEI 或 modem 文件。

## ADB

ADB 管理复用现有 `adbd-init` 和 configfs：读取 `ffs.adb` 链接、监听端口 5555 和 `adbd` 状态；控制 USB ADB、无线 ADB 或重启时会提示当前管理连接可能中断。
