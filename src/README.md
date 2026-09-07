# 后端构建与当前设备说明

后端是 C/GLib/D-Bus 服务，目标架构为 aarch64。

```sh
cd src
make
```

当前 `sz50` 分支额外包含 LED、按键、WiFi、接入设备和恢复出厂 API。设备能力由 `system/device_profile.c` 在运行时探测，不把 `wlan0`、固定 LED 节点或默认 USB VID/PID 当作当前设备事实。

当前设备没有内置电池，Type-C 使用 `0x2dee:0x4d51` RNDIS gadget；RJ45 物理口只有在固件提供有线接口或 `/dev/seth_lte` 后才会被报告为可用。
