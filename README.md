# 5G MiFi Dashboard(UDX710)

[🇨🇳 中文文档](README_CN.md)

A web-based management interface for 5G MiFi devices running on embedded Linux systems (aarch64).

> ⭐ **If you find this project useful, please give it a star!** It took a week of hard work to build this backend. Your support means a lot!

## 📦 Versions

This project provides two versions for different devices:

| Version | Target Device | Git Branch | Features | Description |
|:---:|:---:|:---:|:---:|:---|
| **UDX710 Generic** | UNISOC UDX710 Platform | `main` | ⭐ Basic Features | For most UDX710 devices |
| **SZ50 Dedicated** | SZ50 MiFi Device | `SZ50` | 🌟 Full Features | Extra: LED Control, Key Listener, WiFi Control, Factory Reset, Client Management |

> 💡 **Switch Version**: `git switch sz50` for the current-device build, `git switch main` for the generic version.

### Current `sz50` device profile

This branch targets the externally powered router-shaped UDX710 `marlin3e` device:

- No internal battery; charging controls report as unsupported and the UI shows external power.
- WiFi is detected from hostapd at runtime; the current device uses `wlan1` and `/mnt/data/hostapd_wlan1.conf`.
- Cellular data uses `sipa_eth0` when available.
- Type-C currently exposes vendor RNDIS + ADB + serial functions (`0x2dee:0x4d51`); the dashboard reports the mode but does not attempt generic USB switching.
- RJ45 hardware is present, while the current firmware does not expose `eth0` or `/dev/seth_lte`; the dashboard distinguishes physical presence from usable networking.
- `tools/start.sh` starts the server with `start-stop-daemon`; `tools/rj45_bridge_watch.sh` bridges a future `eth*`/`en*`/`lan*` interface when the device exposes one.

The complete capability matrix is in [`tools/device-capabilities.md`](tools/device-capabilities.md).

### 📥 Download

| Version | Download |
|:---:|:---:|
| **UDX710 Generic** | [📥 Download](https://github.com/LeoChen-CoreMind/UDX710-TOOLS/releases/latest) |
| **SZ50 Dedicated** | [📥 Download](https://github.com/LeoChen-CoreMind/UDX710-TOOLS/releases/latest) |

### SZ50 Dedicated Version Extra Features
- 🔆 **LED Control** - Customize LED indicator status
- 🔘 **Key Listener** - Physical button event response
- 📶 **WiFi Control** - Full WiFi AP management
- 🔄 **Factory Reset** - One-click restore to defaults
- 👥 **Client Management** - Manage connected devices

## ✨ Performance Highlights

| Metric | This Project | Traditional (8080) |
|--------|-------------|-------------------|
| **Binary Size** | ~200 KB | ~6 MB |
| **Memory Usage** (7h runtime) | ~1 MB | Much higher |

Lightweight, efficient, and perfect for resource-constrained embedded devices!

## 📸 Screenshots

| System Monitor | Network Management | Advanced Network |
|:---:|:---:|:---:|
| <img src="docs/screenshot1.png" width="250" /> | <img src="docs/screenshot2.png" width="250" /> | <img src="docs/screenshot3.png" width="250" /> |

| SMS Management | Traffic Statistics | Charge Control |
|:---:|:---:|:---:|
| <img src="docs/screenshot5.png" width="250" /> | <img src="docs/screenshot6.png" width="250" /> | <img src="docs/screenshot7.png" width="250" /> |

| System Update | AT Debug | Web Terminal |
|:---:|:---:|:---:|
| <img src="docs/screenshot8.png" width="250" /> | <img src="docs/screenshot9.png" width="250" /> | <img src="docs/screenshot10.png" width="250" /> |

| USB Mode | System Settings |
|:---:|:---:|
| <img src="docs/screenshot11.png" width="250" /> | <img src="docs/screenshot12.png" width="250" /> |

| APN Settings | Plugin Store |
|:---:|:---:|
| <img src="docs/screenshot13.png" width="250" /> | <img src="docs/screenshot14.png" width="250" /> |

## Features

### Network Management
- **Modem Control**: View IMEI, ICCID, carrier info, signal strength
- **Band Information**: Real-time display of network type, band, ARFCN, PCI, RSRP, RSRQ, SINR
- **Cell Management**: View and manage cellular connections
- **Traffic Statistics**: Monitor data usage with vnstat integration
- **Traffic Control**: Set data limits and automatic network cutoff

### WiFi Management
- **AP Mode**: Configure WiFi hotspot (SSID, password, channel)
- **Client Management**: View connected devices, kick clients
- **DHCP Settings**: Configure IP range and lease time

### System Features
- **System Monitor**: CPU, memory, temperature monitoring (IMEI/ICCID privacy masking)
- **SMS Management**: Send and receive SMS messages, Webhook forwarding support
- **IPv6 Port Forwarding**: Full IPv6 port forwarding service
  - Map local IPv4 ports to IPv6 addresses
  - Automatic IPv6 address detection
  - Webhook notification for IPv6 address changes
  - Auto-start on boot support
  - Send logs with response tracking
- **Intranet Penetration (Rathole)**: Built-in Rathole client for NAT traversal
  - Multi-service configuration
  - Auto-generate server config
  - Real-time connection status
  - Auto-start on boot support
- **LED Control**: Manage device LED indicators
- **Airplane Mode**: Toggle airplane mode
- **Power Management**: Battery status, charging control
- **USB Mode Switch**: Switch between CDC-ECM, CDC-NCM, RNDIS USB network modes
  - Temporary mode: Effective after reboot, reverts on next reboot
  - Permanent mode: Persists across all reboots
- **APN Settings**: Custom APN access point configuration
  - Preset carrier configurations (China Mobile/Unicom/Telecom)
  - Custom APN, username, password
  - Multiple authentication protocols (PAP/CHAP)
- **Plugin Store**: Extensible plugin system
  - Support custom JS+HTML plugins
  - Built-in Shell script execution API
  - Script management (upload/edit/delete)
  - Plugin import/export functionality
- **OTA Update**: Over-the-air firmware updates
- **Factory Reset**: Restore device to default settings
- **Web Terminal**: Remote shell access
- **AT Debug**: Direct AT command interface

### UI Features
- **Dark Mode**: Full dark/light theme support
- **Responsive Design**: Mobile and desktop optimized
- **Real-time Updates**: Live data refresh
- **Chinese Interface**: Native Chinese language support

### Security Features
- **Backend Authentication**: Password-protected admin interface
  - Default password: `admin` (recommended to change after first login)
  - Token-based authentication with auto-expiration
  - Remember password option
  - Password change support

## Architecture

```
├── src/                    # Backend (C)
│   ├── main.c              # Entry point
│   ├── mongoose.c/h        # HTTP server (Mongoose)
│   ├── packed_fs.c         # Embedded static files
│   ├── handlers/           # HTTP API handlers
│   │   ├── http_server.c   # Route definitions
│   │   └── handlers.c      # API implementations
│   └── system/             # System modules
│       ├── sysinfo.c       # System information
│       ├── wifi.c          # WiFi control
│       ├── sms.c           # SMS management
│       ├── traffic.c       # Traffic statistics
│       ├── modem.c         # Modem control
│       ├── ofono.c         # oFono D-Bus integration
│       ├── led.c           # LED control
│       ├── charge.c        # Battery management
│       ├── airplane.c      # Airplane mode
│       ├── usb_mode.c      # USB mode switch
│       ├── plugin.c        # Plugin system
│       ├── update.c        # OTA updates
│       ├── factory_reset.c # Factory reset
│       └── ...
└── web/                    # Frontend (Vue 3)
    ├── src/
    │   ├── App.vue         # Main application
    │   ├── components/     # Vue components
    │   ├── composables/    # Vue composables
    │   └── plugins/        # Plugins (FontAwesome)
    ├── index.html
    ├── package.json
    ├── vite.config.js
    └── tailwind.config.js
```

## Requirements

### Backend
- GCC cross-compiler (aarch64-linux-gnu)
- GLib 2.0 (D-Bus support)
- Target: Linux aarch64 (embedded device)

### Frontend
- Node.js 18+
- npm or yarn

## Build Instructions

### Frontend
```bash
cd web
npm install
npm run build
```

### Backend
```bash
# Pack frontend into C source
cd src
# Generate packed_fs.c from web/dist

# Cross-compile for aarch64
make
```

### Makefile Configuration
The backend uses cross-compilation targeting aarch64-linux-gnu. Ensure your toolchain is properly configured.

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/sysinfo` | GET | System information |
| `/api/wifi/config` | GET/POST | WiFi configuration |
| `/api/wifi/clients` | GET | Connected clients |
| `/api/sms/list` | GET | SMS messages |
| `/api/sms/send` | POST | Send SMS |
| `/api/traffic/stats` | GET | Traffic statistics |
| `/api/traffic/limit` | POST | Set traffic limit |
| `/api/modem/info` | GET | Modem information |
| `/api/band/current` | GET | Current band info |
| `/api/led/status` | GET/POST | LED control |
| `/api/airplane` | GET/POST | Airplane mode |
| `/api/usb/mode` | GET/POST | USB mode switch (CDC-ECM/CDC-NCM/RNDIS) |
| `/api/apn` | GET/POST | APN configuration management |
| `/api/plugins` | GET/POST/DELETE | Plugin management |
| `/api/scripts` | GET/POST/PUT/DELETE | Script management |
| `/api/shell` | POST | Execute Shell commands |
| `/api/ipv6-proxy/config` | GET/POST | IPv6 proxy configuration |
| `/api/ipv6-proxy/rules` | GET/POST/DELETE | IPv6 forwarding rules |
| `/api/ipv6-proxy/status` | GET | IPv6 proxy status |
| `/api/ipv6-proxy/send-logs` | GET | IPv6 send logs |
| `/api/rathole/config` | GET/POST | Rathole configuration |
| `/api/rathole/services` | GET/POST/DELETE | Rathole service management |
| `/api/rathole/status` | GET | Rathole connection status |
| `/api/rathole/logs` | GET | Rathole logs |
| `/api/update/check` | GET | Check for updates |
| `/api/update/install` | POST | Install update |
| `/api/factory-reset` | POST | Factory reset |
| `/api/reboot` | POST | Reboot device |

## Dependencies

### Backend Libraries
- [Mongoose](https://github.com/cesanta/mongoose) - Embedded HTTP server
- GLib/GIO - D-Bus communication with oFono

### Frontend Libraries
- Vue 3 - UI framework
- Vite - Build tool
- TailwindCSS - Styling
- FontAwesome - Icons

## 🌐 Remote Management

Built-in lightweight Web Server for browser-based control interface.

**Features**: Device status cards, real-time monitoring, network control & debugging

| Version | Default Access |
|:---:|:---|
| UDX710 Generic | `http://DEVICE_IP:6677` |
| SZ50 Dedicated | `http://DEVICE_IP:80` |

```bash
# Start server (default port)
./server

# Start with custom port
./server 80
```

## 📜 License

This project is licensed under **GPLv3** (strong Copyleft):

| ✅ Allowed | ⚠️ Required | ❌ Prohibited |
|:---|:---|:---|
| Use, modify, distribute | Keep copyright notices | Closed-source commercialization |
| Distribute modified versions | Open source (when distributing) | Remove copyright info |
| | Use same license | Change to other licenses |

See [LICENSE](LICENSE)

## 🙏 Acknowledgments

Special thanks to the following contributors:

| Contributor | Contribution |
|:---:|:---|
| **等不住** | AT Commands |
| **黑衣剑士** | USB Mode Switch |
| **Voodoo** | Glib Build Environment |
| **1orz** | [project-cpe](https://github.com/1orz/project-cpe) Open Source Project |
| **LeoChen** | Project Author |

Thanks to all community members for your support and feedback!

## ☕ Support the Project

This project is completely open source and free. If you like this project, you can buy me a coffee~

| Alipay | WeChat | QQ Group |
|:---:|:---:|:---:|
| <img src="docs/alipay.png" width="200" /> | <img src="docs/wechat.png" width="200" /> | <img src="docs/qq_group.png" width="200" /> |

## 💬 Community

Welcome to join the discussion!

- **QQ Group**: 1029148488

Welcome to submit Issues / Pull Requests to improve the project 💡
