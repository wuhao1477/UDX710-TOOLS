#define _POSIX_C_SOURCE 200809L

#include "device_profile.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LED_ROOT "/sys/class/leds"
#define NET_ROOT "/sys/class/net"
#define POWER_ROOT "/sys/class/power_supply"
#define GADGET_ROOT "/sys/kernel/config/usb_gadget/g1"

static DeviceProfile profile;
static int initialized;

static void copy_text(char *out, size_t out_size, const char *value) {
  if (out_size == 0) {
    return;
  }
  if (!value) {
    out[0] = '\0';
    return;
  }
  snprintf(out, out_size, "%s", value);
}

static int has_prefix(const char *value, const char *prefix) {
  return value && prefix && strncmp(value, prefix, strlen(prefix)) == 0;
}

static int has_name(const char *const *names, size_t count, const char *target) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(names[i], target) == 0) {
      return 1;
    }
  }
  return 0;
}

int device_profile_parse_hostapd(const char *cmdline, char *iface,
                                 size_t iface_size, char *config,
                                 size_t config_size) {
  char buffer[512];
  char *save = NULL;
  char *token;
  int found_iface = 0;
  int found_config = 0;

  if (!cmdline || !iface || !config || iface_size == 0 || config_size == 0) {
    return -1;
  }
  copy_text(iface, iface_size, "");
  copy_text(config, config_size, "");
  copy_text(buffer, sizeof(buffer), cmdline);

  token = strtok_r(buffer, " \t\r\n", &save);
  while (token) {
    if (strcmp(token, "-i") == 0) {
      token = strtok_r(NULL, " \t\r\n", &save);
      if (token) {
        copy_text(iface, iface_size, token);
        found_iface = 1;
      }
    } else if (strstr(token, "hostapd") && strstr(token, ".conf")) {
      copy_text(config, config_size, token);
      found_config = 1;
    }
    token = strtok_r(NULL, " \t\r\n", &save);
  }
  return found_iface && found_config ? 0 : -1;
}

int device_profile_select_led(const char *const *names, size_t count,
                              const char *color, char *out, size_t out_size) {
  const char *preferred[2] = {NULL, NULL};

  if (!names || !color || !out || out_size == 0) {
    return -1;
  }
  if (strcmp(color, "red") == 0) {
    preferred[0] = "red_led";
    preferred[1] = "sc27xx:red";
  } else if (strcmp(color, "green") == 0) {
    preferred[0] = "green_led";
    preferred[1] = "sc27xx:green";
  } else if (strcmp(color, "blue") == 0) {
    preferred[0] = "blue_led";
    preferred[1] = "sc27xx:blue";
  } else {
    return -1;
  }

  for (size_t i = 0; i < 2; i++) {
    if (has_name(names, count, preferred[i])) {
      copy_text(out, out_size, preferred[i]);
      return 0;
    }
  }
  return -1;
}

int device_profile_select_data_iface(const char *const *names, size_t count,
                                     char *out, size_t out_size) {
  if (!names || !out || out_size == 0) {
    return -1;
  }
  if (has_name(names, count, "sipa_eth0")) {
    copy_text(out, out_size, "sipa_eth0");
    return 0;
  }
  for (size_t i = 0; i < count; i++) {
    if (has_prefix(names[i], "sipa_eth")) {
      copy_text(out, out_size, names[i]);
      return 0;
    }
  }
  for (size_t i = 0; i < count; i++) {
    if (has_prefix(names[i], "wwan") || has_prefix(names[i], "rmnet")) {
      copy_text(out, out_size, names[i]);
      return 0;
    }
  }
  copy_text(out, out_size, "");
  return -1;
}

static int usb_function_matches(const char *const *functions, size_t count,
                                const char *name) {
  return functions && has_name(functions, count, name);
}

DeviceUsbMode device_profile_classify_usb(const char *vid, const char *pid,
                                          const char *const *functions,
                                          size_t count) {
  if (!vid || !pid) {
    return DEVICE_USB_UNKNOWN;
  }
  if (strcmp(vid, "0x2dee") == 0 && strcmp(pid, "0x4d51") == 0 &&
      usb_function_matches(functions, count, "rndis.gs4")) {
    return DEVICE_USB_RNDIS_CURRENT;
  }
  if (strcmp(vid, "0x1782") == 0 &&
      ((strcmp(pid, "0x4038") == 0 &&
        usb_function_matches(functions, count, "rndis.gs4")) ||
       (strcmp(pid, "0x4039") == 0 &&
        usb_function_matches(functions, count, "ecm.gs0")) ||
       (strcmp(pid, "0x4040") == 0 &&
        usb_function_matches(functions, count, "ncm.gs0")))) {
    return DEVICE_USB_SWITCHABLE_GENERIC;
  }
  return DEVICE_USB_UNKNOWN;
}

int device_profile_parse_link_state(const char *carrier,
                                    const char *operstate) {
  if (carrier && strcmp(carrier, "1") == 0) return 1;
  if (carrier && strcmp(carrier, "0") == 0) return 0;
  return operstate && strcmp(operstate, "up") == 0;
}

static int read_line(const char *path, char *out, size_t out_size) {
  FILE *file = fopen(path, "r");
  if (!file) {
    return -1;
  }
  if (!fgets(out, (int)out_size, file)) {
    fclose(file);
    return -1;
  }
  fclose(file);
  out[strcspn(out, "\r\n")] = '\0';
  return 0;
}

static int interface_link_up(const char *name) {
  char path[256];
  char carrier[16] = {0};
  char operstate[16] = {0};
  snprintf(path, sizeof(path), "%s/%s/carrier", NET_ROOT, name);
  read_line(path, carrier, sizeof(carrier));
  snprintf(path, sizeof(path), "%s/%s/operstate", NET_ROOT, name);
  read_line(path, operstate, sizeof(operstate));
  return device_profile_parse_link_state(carrier, operstate);
}

static int select_rj45_runtime(const char *const *names, size_t count,
                               char *out, size_t out_size) {
  const char *fallback = NULL;
  for (size_t i = 0; i < count; i++) {
    if (!has_prefix(names[i], "eth") && !has_prefix(names[i], "en") &&
        !has_prefix(names[i], "lan")) {
      continue;
    }
    if (!fallback) fallback = names[i];
    if (interface_link_up(names[i])) {
      copy_text(out, out_size, names[i]);
      return 0;
    }
  }
  if (fallback) {
    copy_text(out, out_size, fallback);
    return 0;
  }
  copy_text(out, out_size, "");
  return -1;
}

static int directory_has_entry(const char *path, const char *wanted) {
  DIR *dir = opendir(path);
  struct dirent *entry;
  if (!dir) {
    return 0;
  }
  while ((entry = readdir(dir))) {
    if (strcmp(entry->d_name, wanted) == 0) {
      closedir(dir);
      return 1;
    }
  }
  closedir(dir);
  return 0;
}

static size_t list_directory(const char *path, char names[][64], size_t limit) {
  DIR *dir = opendir(path);
  struct dirent *entry;
  size_t count = 0;
  if (!dir) {
    return 0;
  }
  while (count < limit && (entry = readdir(dir))) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    copy_text(names[count], sizeof(names[count]), entry->d_name);
    count++;
  }
  closedir(dir);
  return count;
}

static int modem_declares_ethernet(void) {
  FILE *file = fopen("/etc/modem/modem.ini", "r");
  char line[256];
  if (!file) {
    return 0;
  }
  while (fgets(line, sizeof(line), file)) {
    if (strstr(line, "ro.vendor.modem.eth=seth_lte")) {
      fclose(file);
      return 1;
    }
  }
  fclose(file);
  return 0;
}

static int find_hostapd(char *iface, size_t iface_size, char *config,
                        size_t config_size) {
  FILE *pipe = popen("ps 2>/dev/null", "r");
  char line[512];
  if (!pipe) {
    return -1;
  }
  while (fgets(line, sizeof(line), pipe)) {
    if (strstr(line, "hostapd") &&
        device_profile_parse_hostapd(line, iface, iface_size, config,
                                      config_size) == 0) {
      pclose(pipe);
      return 0;
    }
  }
  pclose(pipe);
  return -1;
}

static void detect_leds(DeviceProfile *out) {
  char names[64][64];
  const char *name_ptrs[64];
  size_t count = list_directory(LED_ROOT, names, 64);
  for (size_t i = 0; i < count; i++) {
    name_ptrs[i] = names[i];
  }
  const char *colors[] = {"red", "green", "blue"};
  char *paths[] = {out->led_red, out->led_green, out->led_blue};
  for (size_t i = 0; i < 3; i++) {
    char name[64];
    if (device_profile_select_led(name_ptrs, count, colors[i], name,
                                  sizeof(name)) == 0) {
      snprintf(paths[i], 256, "%s/%s", LED_ROOT, name);
    }
  }
}

static void detect_usb(DeviceProfile *out) {
  char path[256];
  char functions[32][64];
  char link_targets[32][256];
  const char *function_ptrs[32];
  size_t count;

  out->typec_present = access(GADGET_ROOT, F_OK) == 0 ||
                       access("/sys/class/typec", F_OK) == 0;
  out->typec_host_capable = access("/sys/bus/usb/devices", F_OK) == 0;
  if (!out->typec_present) {
    copy_text(out->reason_usb, sizeof(out->reason_usb), "未检测到 Type-C gadget 或 typec 节点");
    return;
  }

  snprintf(path, sizeof(path), "%s/idVendor", GADGET_ROOT);
  if (read_line(path, out->usb_vid, sizeof(out->usb_vid)) != 0) {
    copy_text(out->usb_vid, sizeof(out->usb_vid), "");
  }
  snprintf(path, sizeof(path), "%s/idProduct", GADGET_ROOT);
  if (read_line(path, out->usb_pid, sizeof(out->usb_pid)) != 0) {
    copy_text(out->usb_pid, sizeof(out->usb_pid), "");
  }

  count = list_directory(GADGET_ROOT "/configs/b.1", functions, 32);
  size_t function_count = 0;
  for (size_t i = 0; i < count; i++) {
    snprintf(path, sizeof(path), "%s/configs/b.1/%s", GADGET_ROOT,
             functions[i]);
    ssize_t link_len = readlink(path, link_targets[function_count],
                                sizeof(link_targets[function_count]) - 1);
    if (link_len <= 0) {
      continue;
    }
    link_targets[function_count][link_len] = '\0';
    const char *name = strrchr(link_targets[function_count], '/');
    function_ptrs[function_count] =
        name ? name + 1 : link_targets[function_count];
    function_count++;
  }
  DeviceUsbMode mode = device_profile_classify_usb(
      out->usb_vid, out->usb_pid, function_ptrs, function_count);
  out->usb_rndis_available = mode == DEVICE_USB_RNDIS_CURRENT ||
                             mode == DEVICE_USB_SWITCHABLE_GENERIC;
  out->usb_mode_switch_supported = mode == DEVICE_USB_SWITCHABLE_GENERIC;
  if (!out->usb_mode_switch_supported) {
    copy_text(out->reason_usb, sizeof(out->reason_usb),
              "当前 Type-C 组合为厂商 RNDIS/ADB/串口，未启用通用热切换");
  }
}

int device_profile_refresh(void) {
  char names[64][64];
  const char *name_ptrs[64];
  char hostapd_iface[32];
  char hostapd_config[256];
  memset(&profile, 0, sizeof(profile));

  if (read_line("/proc/device-tree/model", profile.model,
                sizeof(profile.model)) != 0) {
    read_line("/sys/devices/virtual/dmi/id/product_name", profile.model,
              sizeof(profile.model));
  }

  profile.battery_supported = directory_has_entry(POWER_ROOT, "battery");
  profile.mains_powered = !profile.battery_supported;
  if (profile.mains_powered) {
    copy_text(profile.reason_power, sizeof(profile.reason_power),
              "未检测到 battery，按外部供电路由器处理");
  }

  size_t net_count = list_directory(NET_ROOT, names, 64);
  for (size_t i = 0; i < net_count; i++) {
    name_ptrs[i] = names[i];
  }
  device_profile_select_data_iface(name_ptrs, net_count, profile.data_iface,
                                   sizeof(profile.data_iface));
  if (select_rj45_runtime(name_ptrs, net_count, profile.rj45_iface,
                          sizeof(profile.rj45_iface)) == 0) {
    profile.rj45_interface_present = 1;
    profile.rj45_link_up = interface_link_up(profile.rj45_iface);
  }

  profile.rj45_physical_present = modem_declares_ethernet();
  profile.rj45_usable = profile.rj45_interface_present ||
                        access("/dev/seth_lte", F_OK) == 0;
  if (profile.rj45_usable && profile.rj45_link_up) {
    copy_text(profile.reason_rj45, sizeof(profile.reason_rj45), "有线接口已连接");
  } else if (profile.rj45_usable) {
    copy_text(profile.reason_rj45, sizeof(profile.reason_rj45),
              "有线接口已识别，当前未连接");
  } else if (profile.rj45_physical_present) {
    copy_text(profile.reason_rj45, sizeof(profile.reason_rj45),
              "物理 RJ45 存在，但当前固件没有 eth*/en*/lan* 或 /dev/seth_lte");
  } else {
    copy_text(profile.reason_rj45, sizeof(profile.reason_rj45),
              "未检测到 RJ45 的系统侧描述");
  }

  if (find_hostapd(hostapd_iface, sizeof(hostapd_iface), hostapd_config,
                   sizeof(hostapd_config)) == 0) {
    copy_text(profile.wifi_iface, sizeof(profile.wifi_iface), hostapd_iface);
    copy_text(profile.wifi_config, sizeof(profile.wifi_config), hostapd_config);
  } else if (directory_has_entry(NET_ROOT, "wlan1")) {
    copy_text(profile.wifi_iface, sizeof(profile.wifi_iface), "wlan1");
  } else if (directory_has_entry(NET_ROOT, "wlan0")) {
    copy_text(profile.wifi_iface, sizeof(profile.wifi_iface), "wlan0");
  }

  detect_leds(&profile);
  detect_usb(&profile);
  initialized = 1;
  return 0;
}

int device_profile_init(void) {
  return device_profile_refresh();
}

const DeviceProfile *device_profile_get(void) {
  if (!initialized) {
    device_profile_init();
  }
  return &profile;
}
