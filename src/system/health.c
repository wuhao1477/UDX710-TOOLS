/**
 * @file health.c
 * @brief Real-time device health collection.
 *
 * All data is read directly from procfs/sysfs/statvfs during the request.
 * This module deliberately has no persistent state and never writes files.
 */

#include "health.h"

#include "http_utils.h"
#include "json_builder.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#define HEALTH_PATH_SIZE 320
#define MAX_THERMAL_SENSORS 32
#define MAX_THERMAL_TRIPS 8
#define MAX_STORAGE_MOUNTS 8
#define MAX_USB_DEVICES 32
#define MAX_NETWORK_INTERFACES 32

typedef struct {
  char type[32];
  long long millidegrees;
} ThermalTrip;

typedef struct {
  char zone[64];
  char raw_type[128];
  char source[128];
  char component[128];
  char area[128];
  char confidence[16];
  char sensor_kind[32];
  long long millidegrees;
  long long warning_millidegrees;
  long long hot_millidegrees;
  long long critical_millidegrees;
  int derived;
  ThermalTrip trips[MAX_THERMAL_TRIPS];
  int trip_count;
} ThermalSensor;

typedef struct {
  char path[HEALTH_PATH_SIZE];
  int mounted;
  int readonly;
  unsigned long long total_bytes;
  unsigned long long used_bytes;
  unsigned long long available_bytes;
  double use_percent;
} StorageStat;

typedef struct {
  char name[64];
  char address[64];
  char operstate[32];
  char duplex[32];
  char driver[64];
  long long speed_mbps;
  long long carrier;
  long long carrier_changes;
  long long rx_bytes;
  long long tx_bytes;
  long long rx_packets;
  long long tx_packets;
  long long rx_errors;
  long long tx_errors;
  long long rx_dropped;
  long long tx_dropped;
  int speed_available;
  int carrier_available;
  int carrier_changes_available;
} NetworkInterfaceHealth;

static int read_text(const char *path, char *buf, size_t size) {
  FILE *file;
  size_t length;

  if (!path || !buf || size < 2) return -1;
  file = fopen(path, "r");
  if (!file) return -1;

  length = fread(buf, 1, size - 1, file);
  fclose(file);
  buf[length] = '\0';

  while (length > 0 && (buf[length - 1] == '\n' ||
                        buf[length - 1] == '\r' ||
                        buf[length - 1] == ' ' ||
                        buf[length - 1] == '\t')) {
    buf[--length] = '\0';
  }
  return length > 0 ? 0 : -1;
}

static int read_number(const char *path, long long *value) {
  char buf[64];
  char *end = NULL;
  long long parsed;

  if (!value || read_text(path, buf, sizeof(buf)) != 0) return -1;
  errno = 0;
  parsed = strtoll(buf, &end, 10);
  if (errno != 0 || end == buf) return -1;
  *value = parsed;
  return 0;
}

static int join_path(char *dest, size_t size, const char *left,
                     const char *right) {
  size_t left_len;
  size_t right_len;

  if (!dest || size == 0 || !left || !right) return -1;
  left_len = strlen(left);
  right_len = strlen(right);
  if (left_len + right_len + 1 > size) return -1;
  memcpy(dest, left, left_len);
  memcpy(dest + left_len, right, right_len);
  dest[left_len + right_len] = '\0';
  return 0;
}

static int join_sysfs_name(char *dest, size_t size, const char *prefix,
                           const char *name, const char *suffix) {
  size_t prefix_len;
  size_t name_len;
  size_t suffix_len;

  if (!dest || size == 0 || !prefix || !name || !suffix) return -1;
  prefix_len = strlen(prefix);
  name_len = strlen(name);
  suffix_len = strlen(suffix);
  if (prefix_len + name_len + suffix_len + 1 > size) return -1;
  memcpy(dest, prefix, prefix_len);
  memcpy(dest + prefix_len, name, name_len);
  memcpy(dest + prefix_len + name_len, suffix, suffix_len);
  dest[prefix_len + name_len + suffix_len] = '\0';
  return 0;
}

static int append_text(char *dest, size_t size, const char *text) {
  size_t dest_len;
  size_t text_len;

  if (!dest || size == 0 || !text) return -1;
  dest_len = strlen(dest);
  text_len = strlen(text);
  if (dest_len + text_len + 1 > size) return -1;
  memcpy(dest + dest_len, text, text_len);
  dest[dest_len + text_len] = '\0';
  return 0;
}

static int path_exists(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0;
}

static int read_link_name(const char *path, char *buf, size_t size) {
  ssize_t length;
  const char *last_slash;

  if (!path || !buf || size < 2) return -1;
  length = readlink(path, buf, size - 1);
  if (length < 0) return -1;
  buf[length] = '\0';

  last_slash = strrchr(buf, '/');
  if (last_slash && last_slash[1] != '\0') {
    memmove(buf, last_slash + 1, strlen(last_slash + 1) + 1);
  }
  return 0;
}

static void copy_text(char *dest, size_t size, const char *src) {
  size_t length;

  if (!dest || size == 0) return;
  if (!src) src = "";
  length = strlen(src);
  if (length >= size) length = size - 1;
  memcpy(dest, src, length);
  dest[length] = '\0';
}

static void classify_thermal(const char *raw_type, ThermalSensor *sensor) {
  const char *suffix;

  copy_text(sensor->confidence, sizeof(sensor->confidence), "low");
  copy_text(sensor->sensor_kind, sizeof(sensor->sensor_kind), "platform_channel");
  sensor->derived = 0;

  if (strcmp(raw_type, "soc-thmzone") == 0) {
    copy_text(sensor->component, sizeof(sensor->component), "UDX710 SoC");
    copy_text(sensor->area, sizeof(sensor->area), "main system-on-chip");
    copy_text(sensor->confidence, sizeof(sensor->confidence), "high");
    copy_text(sensor->sensor_kind, sizeof(sensor->sensor_kind), "SoC sensor");
  } else if (strcmp(raw_type, "apcpu0-thmzone") == 0) {
    copy_text(sensor->component, sizeof(sensor->component), "Application CPU 0");
    copy_text(sensor->area, sizeof(sensor->area), "AP CPU");
    copy_text(sensor->confidence, sizeof(sensor->confidence), "high");
    copy_text(sensor->sensor_kind, sizeof(sensor->sensor_kind), "CPU sensor");
  } else if (strcmp(raw_type, "apcpu1-thmzone") == 0) {
    copy_text(sensor->component, sizeof(sensor->component), "Application CPU 1");
    copy_text(sensor->area, sizeof(sensor->area), "AP CPU");
    copy_text(sensor->confidence, sizeof(sensor->confidence), "high");
    copy_text(sensor->sensor_kind, sizeof(sensor->sensor_kind), "CPU sensor");
  } else if (strcmp(raw_type, "cluster0-thmzone") == 0) {
    copy_text(sensor->component, sizeof(sensor->component), "AP CPU cluster 0");
    copy_text(sensor->area, sizeof(sensor->area), "CPU cluster control zone");
    copy_text(sensor->confidence, sizeof(sensor->confidence), "high");
    copy_text(sensor->sensor_kind, sizeof(sensor->sensor_kind), "derived thermal zone");
    sensor->derived = 1;
  } else if (strcmp(raw_type, "nrcp-thmzone") == 0) {
    copy_text(sensor->component, sizeof(sensor->component), "NRCP communication processor");
    copy_text(sensor->area, sizeof(sensor->area), "cellular / 5G processing");
    copy_text(sensor->confidence, sizeof(sensor->confidence), "medium");
    copy_text(sensor->sensor_kind, sizeof(sensor->sensor_kind), "platform sensor");
  } else if (strcmp(raw_type, "pa-thmzone") == 0) {
    copy_text(sensor->component, sizeof(sensor->component), "Cellular RF power amplifier");
    copy_text(sensor->area, sizeof(sensor->area), "4G / LTE RF");
    copy_text(sensor->confidence, sizeof(sensor->confidence), "medium");
    copy_text(sensor->sensor_kind, sizeof(sensor->sensor_kind), "RF sensor");
  } else if (strcmp(raw_type, "pa5g-thmzone") == 0) {
    copy_text(sensor->component, sizeof(sensor->component), "5G RF power amplifier");
    copy_text(sensor->area, sizeof(sensor->area), "5G RF");
    copy_text(sensor->confidence, sizeof(sensor->confidence), "medium");
    copy_text(sensor->sensor_kind, sizeof(sensor->sensor_kind), "RF sensor");
  } else if (strcmp(raw_type, "board-thmzone") == 0) {
    copy_text(sensor->component, sizeof(sensor->component), "Board temperature sensor");
    copy_text(sensor->area, sizeof(sensor->area), "main board / NTC or ADC");
    copy_text(sensor->confidence, sizeof(sensor->confidence), "medium");
    copy_text(sensor->sensor_kind, sizeof(sensor->sensor_kind), "board sensor");
  } else if (strncmp(raw_type, "ank", 3) == 0 &&
             strstr(raw_type, "-thmzone") != NULL) {
    suffix = raw_type + 3;
    copy_text(sensor->component, sizeof(sensor->component),
              "Unisoc private internal thermal channel");
    snprintf(sensor->area, sizeof(sensor->area), "internal channel %.*s; physical area unconfirmed",
             (int)(strstr(suffix, "-thmzone") - suffix), suffix);
    copy_text(sensor->confidence, sizeof(sensor->confidence), "low");
    copy_text(sensor->sensor_kind, sizeof(sensor->sensor_kind), "vendor-private channel");
  } else {
    copy_text(sensor->component, sizeof(sensor->component), "Kernel thermal zone");
    copy_text(sensor->area, sizeof(sensor->area), "unmapped platform area");
  }
}

static void update_thermal_thresholds(ThermalSensor *sensor,
                                       const char *trip_type,
                                       long long trip_millidegrees) {
  if (strcmp(trip_type, "critical") == 0) {
    if (sensor->critical_millidegrees < 0 ||
        trip_millidegrees < sensor->critical_millidegrees) {
      sensor->critical_millidegrees = trip_millidegrees;
    }
    return;
  }

  if (sensor->warning_millidegrees < 0 ||
      trip_millidegrees < sensor->warning_millidegrees) {
    sensor->warning_millidegrees = trip_millidegrees;
  }
  if (strcmp(trip_type, "hot") == 0 &&
      (sensor->hot_millidegrees < 0 ||
       trip_millidegrees < sensor->hot_millidegrees)) {
    sensor->hot_millidegrees = trip_millidegrees;
  }
}

static int collect_thermal(ThermalSensor *sensors, int max_count,
                           long long *max_millidegrees,
                           long long *warning_millidegrees,
                           long long *hot_millidegrees,
                           long long *critical_millidegrees) {
  int zone;
  int count = 0;
  int i;
  char path[PATH_MAX];
  char type[128];

  *max_millidegrees = -1;
  *warning_millidegrees = -1;
  *hot_millidegrees = -1;
  *critical_millidegrees = -1;
  for (zone = 0; zone < MAX_THERMAL_SENSORS && count < max_count; zone++) {
    ThermalSensor *sensor = &sensors[count];

    snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%d/type", zone);
    if (read_text(path, type, sizeof(type)) != 0) continue;

    memset(sensor, 0, sizeof(*sensor));
    snprintf(sensor->zone, sizeof(sensor->zone), "thermal_zone%d", zone);
    copy_text(sensor->raw_type, sizeof(sensor->raw_type), type);
    snprintf(sensor->source, sizeof(sensor->source),
             "/sys/class/thermal/thermal_zone%d/temp", zone);
    sensor->warning_millidegrees = -1;
    sensor->hot_millidegrees = -1;
    sensor->critical_millidegrees = -1;
    classify_thermal(type, sensor);

    if (read_number(sensor->source, &sensor->millidegrees) != 0) {
      sensor->millidegrees = -1;
    } else if (sensor->millidegrees > *max_millidegrees) {
      *max_millidegrees = sensor->millidegrees;
    }

    for (i = 0; i < MAX_THERMAL_TRIPS; i++) {
      char temp_path[PATH_MAX];
      char type_path[PATH_MAX];
      char trip_type[32];
      long long trip_temp;

      snprintf(temp_path, sizeof(temp_path),
               "/sys/class/thermal/thermal_zone%d/trip_point_%d_temp", zone, i);
      if (read_number(temp_path, &trip_temp) != 0) continue;

      snprintf(type_path, sizeof(type_path),
               "/sys/class/thermal/thermal_zone%d/trip_point_%d_type", zone, i);
      if (read_text(type_path, trip_type, sizeof(trip_type)) != 0) {
        copy_text(trip_type, sizeof(trip_type), "unknown");
      }

      if (sensor->trip_count < MAX_THERMAL_TRIPS) {
        copy_text(sensor->trips[sensor->trip_count].type,
                  sizeof(sensor->trips[sensor->trip_count].type), trip_type);
        sensor->trips[sensor->trip_count].millidegrees = trip_temp;
        sensor->trip_count++;
      }

      update_thermal_thresholds(sensor, trip_type, trip_temp);
    }

    if (sensor->warning_millidegrees >= 0 &&
        (*warning_millidegrees < 0 ||
         sensor->warning_millidegrees < *warning_millidegrees)) {
      *warning_millidegrees = sensor->warning_millidegrees;
    }
    if (sensor->hot_millidegrees >= 0 &&
        (*hot_millidegrees < 0 ||
         sensor->hot_millidegrees < *hot_millidegrees)) {
      *hot_millidegrees = sensor->hot_millidegrees;
    }
    if (sensor->critical_millidegrees >= 0 &&
        (*critical_millidegrees < 0 ||
         sensor->critical_millidegrees < *critical_millidegrees)) {
      *critical_millidegrees = sensor->critical_millidegrees;
    }
    count++;
  }
  return count;
}

static const char *thermal_status(const ThermalSensor *sensor) {
  if (!sensor || sensor->millidegrees < 0) return "unknown";
  if (sensor->critical_millidegrees >= 0 &&
      sensor->millidegrees >= sensor->critical_millidegrees) {
    return "critical";
  }
  if ((sensor->hot_millidegrees >= 0 &&
       sensor->millidegrees >= sensor->hot_millidegrees) ||
      (sensor->warning_millidegrees >= 0 &&
       sensor->millidegrees >= sensor->warning_millidegrees)) {
    return "warning";
  }
  if (sensor->warning_millidegrees < 0) return "unknown";
  return "normal";
}

static int collect_storage(StorageStat *stats, int max_count) {
  static const char *paths[] = {
      "/", "/mnt/userdata", "/mnt/data", "/home", "/var",
      "/var/volatile", "/etc/productinfo", "/m_data"};
  int i;
  int count = 0;

  for (i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])) && count < max_count;
       i++) {
    struct statvfs fs;
    StorageStat *item = &stats[count];
    unsigned long long total;
    unsigned long long free_bytes;
    unsigned long long available;

    memset(item, 0, sizeof(*item));
    copy_text(item->path, sizeof(item->path), paths[i]);
    if (statvfs(paths[i], &fs) != 0) continue;

    total = (unsigned long long)fs.f_blocks * fs.f_frsize;
    free_bytes = (unsigned long long)fs.f_bfree * fs.f_frsize;
    available = (unsigned long long)fs.f_bavail * fs.f_frsize;
    item->mounted = 1;
    item->readonly = (fs.f_flag & ST_RDONLY) != 0;
    item->total_bytes = total;
    item->used_bytes = total > free_bytes ? total - free_bytes : 0;
    item->available_bytes = available;
    item->use_percent = total > 0
                            ? ((double)item->used_bytes * 100.0) / (double)total
                            : 0.0;
    count++;
  }
  return count;
}

static void read_interface_number(const char *name, const char *field,
                                  long long *value, int *available) {
  char path[HEALTH_PATH_SIZE];
  if (!name || !field || field[0] == '\0') {
    *value = -1;
    *available = 0;
    return;
  }
  if (join_sysfs_name(path, sizeof(path), "/sys/class/net/", name,
                      "/statistics/") != 0 ||
      append_text(path, sizeof(path), field) != 0) {
    *value = -1;
    *available = 0;
    return;
  }
  if (read_number(path, value) == 0) {
    *available = 1;
  } else {
    *value = -1;
    *available = 0;
  }
}

static int collect_network(NetworkInterfaceHealth *interfaces, int max_count,
                           int *eth0_down) {
  DIR *dir;
  struct dirent *entry;
  int count = 0;

  *eth0_down = 0;
  dir = opendir("/sys/class/net");
  if (!dir) return 0;

  while ((entry = readdir(dir)) != NULL && count < max_count) {
    char path[HEALTH_PATH_SIZE];
    char text[128];
    NetworkInterfaceHealth *item;
    long long number;
    int available;

    if (entry->d_name[0] == '.') continue;
    item = &interfaces[count];
    memset(item, 0, sizeof(*item));
    copy_text(item->name, sizeof(item->name), entry->d_name);

    if (join_sysfs_name(path, sizeof(path), "/sys/class/net/", entry->d_name,
                        "/address") == 0 &&
        read_text(path, text, sizeof(text)) == 0) {
      copy_text(item->address, sizeof(item->address), text);
    }
    if (join_sysfs_name(path, sizeof(path), "/sys/class/net/", entry->d_name,
                        "/operstate") == 0 &&
        read_text(path, text, sizeof(text)) == 0) {
      copy_text(item->operstate, sizeof(item->operstate), text);
    }
    if (join_sysfs_name(path, sizeof(path), "/sys/class/net/", entry->d_name,
                        "/duplex") == 0 &&
        read_text(path, text, sizeof(text)) == 0) {
      copy_text(item->duplex, sizeof(item->duplex), text);
    }
    if (join_sysfs_name(path, sizeof(path), "/sys/class/net/", entry->d_name,
                        "/device/driver") == 0 &&
        read_link_name(path, text, sizeof(text)) == 0) {
      copy_text(item->driver, sizeof(item->driver), text);
    }

    if (join_sysfs_name(path, sizeof(path), "/sys/class/net/", entry->d_name,
                        "/speed") == 0 &&
        read_number(path, &number) == 0) {
      item->speed_mbps = number;
      item->speed_available = 1;
    } else {
      item->speed_mbps = -1;
    }
    if (join_sysfs_name(path, sizeof(path), "/sys/class/net/", entry->d_name,
                        "/carrier") == 0 &&
        read_number(path, &number) == 0) {
      item->carrier = number;
      item->carrier_available = 1;
    } else {
      item->carrier = -1;
    }
    if (join_sysfs_name(path, sizeof(path), "/sys/class/net/", entry->d_name,
                        "/carrier_changes") == 0 &&
        read_number(path, &number) == 0) {
      item->carrier_changes = number;
      item->carrier_changes_available = 1;
    } else {
      item->carrier_changes = -1;
    }

    read_interface_number(entry->d_name, "rx_bytes", &item->rx_bytes, &available);
    read_interface_number(entry->d_name, "tx_bytes", &item->tx_bytes, &available);
    read_interface_number(entry->d_name, "rx_packets", &item->rx_packets, &available);
    read_interface_number(entry->d_name, "tx_packets", &item->tx_packets, &available);
    read_interface_number(entry->d_name, "rx_errors", &item->rx_errors, &available);
    read_interface_number(entry->d_name, "tx_errors", &item->tx_errors, &available);
    read_interface_number(entry->d_name, "rx_dropped", &item->rx_dropped, &available);
    read_interface_number(entry->d_name, "tx_dropped", &item->tx_dropped, &available);

    if (strcmp(entry->d_name, "eth0") == 0) {
      if (item->carrier_available && item->carrier == 0) *eth0_down = 1;
    }
    count++;
  }
  closedir(dir);
  return count;
}

static void add_optional_long(JsonBuilder *j, const char *key, long long value,
                              int available) {
  if (available) {
    json_add_long(j, key, value);
  } else {
    json_add_null(j, key);
  }
}

static void emit_thermal(JsonBuilder *j, ThermalSensor *sensors, int count,
                         long long max_millidegrees,
                         long long warning_millidegrees,
                         long long hot_millidegrees,
                         long long critical_millidegrees) {
  int i;
  int trip;

  json_key_obj_open(j, "thermal");
  if (max_millidegrees >= 0) {
    json_add_double3(j, "max_temperature_celsius",
                     (double)max_millidegrees / 1000.0);
  } else {
    json_add_null(j, "max_temperature_celsius");
  }
  json_key_obj_open(j, "thresholds");
  if (warning_millidegrees >= 0) {
    json_add_double3(j, "warning_celsius",
                     (double)warning_millidegrees / 1000.0);
  } else {
    json_add_null(j, "warning_celsius");
  }
  if (hot_millidegrees >= 0) {
    json_add_double3(j, "hot_celsius", (double)hot_millidegrees / 1000.0);
  } else {
    json_add_null(j, "hot_celsius");
  }
  if (critical_millidegrees >= 0) {
    json_add_double3(j, "critical_celsius",
                     (double)critical_millidegrees / 1000.0);
  } else {
    json_add_null(j, "critical_celsius");
  }
  json_obj_close(j);

  json_arr_open(j, "sensors");
  for (i = 0; i < count; i++) {
    const char *status = thermal_status(&sensors[i]);
    json_arr_obj_open(j);
    json_add_str(j, "zone", sensors[i].zone);
    json_add_str(j, "raw_type", sensors[i].raw_type);
    json_add_str(j, "component", sensors[i].component);
    json_add_str(j, "area", sensors[i].area);
    json_add_str(j, "sensor_kind", sensors[i].sensor_kind);
    json_add_str(j, "mapping_confidence", sensors[i].confidence);
    json_add_bool(j, "derived_value", sensors[i].derived);
    json_add_str(j, "source", sensors[i].source);
    if (sensors[i].millidegrees >= 0) {
      json_add_long(j, "raw_millidegrees",
                    sensors[i].millidegrees);
      json_add_double3(j, "temperature_celsius",
                       (double)sensors[i].millidegrees / 1000.0);
    } else {
      json_add_null(j, "raw_millidegrees");
      json_add_null(j, "temperature_celsius");
    }
    json_add_str(j, "status", status);
    json_arr_open(j, "trip_points");
    for (trip = 0; trip < sensors[i].trip_count; trip++) {
      json_arr_obj_open(j);
      json_add_str(j, "type", sensors[i].trips[trip].type);
      json_add_double3(j, "temperature_celsius",
                       (double)sensors[i].trips[trip].millidegrees / 1000.0);
      json_add_long(j, "raw_millidegrees",
                    sensors[i].trips[trip].millidegrees);
      json_obj_close(j);
    }
    json_arr_close(j);
    json_obj_close(j);
  }
  json_arr_close(j);
  json_obj_close(j);
}

static void emit_storage(JsonBuilder *j, StorageStat *stats, int count) {
  int i;

  json_key_obj_open(j, "storage");
  json_arr_open(j, "mounts");
  for (i = 0; i < count; i++) {
    json_arr_obj_open(j);
    json_add_str(j, "path", stats[i].path);
    json_add_bool(j, "mounted", stats[i].mounted);
    json_add_bool(j, "readonly", stats[i].readonly);
    json_add_long(j, "total_bytes", (long long)stats[i].total_bytes);
    json_add_long(j, "used_bytes", (long long)stats[i].used_bytes);
    json_add_long(j, "available_bytes", (long long)stats[i].available_bytes);
    json_add_double3(j, "use_percent", stats[i].use_percent);
    json_obj_close(j);
  }
  json_arr_close(j);
  json_obj_close(j);
}

static void emit_network(JsonBuilder *j, NetworkInterfaceHealth *interfaces,
                         int count) {
  int i;

  json_key_obj_open(j, "network");
  json_arr_open(j, "interfaces");
  for (i = 0; i < count; i++) {
    json_arr_obj_open(j);
    json_add_str(j, "name", interfaces[i].name);
    json_add_str(j, "address", interfaces[i].address);
    json_add_str(j, "operstate", interfaces[i].operstate);
    json_add_str(j, "duplex", interfaces[i].duplex);
    json_add_str(j, "driver", interfaces[i].driver);
    add_optional_long(j, "speed_mbps", interfaces[i].speed_mbps,
                      interfaces[i].speed_available);
    add_optional_long(j, "carrier", interfaces[i].carrier,
                      interfaces[i].carrier_available);
    add_optional_long(j, "carrier_changes", interfaces[i].carrier_changes,
                      interfaces[i].carrier_changes_available);
    json_key_obj_open(j, "statistics");
    add_optional_long(j, "rx_bytes", interfaces[i].rx_bytes,
                      interfaces[i].rx_bytes >= 0);
    add_optional_long(j, "tx_bytes", interfaces[i].tx_bytes,
                      interfaces[i].tx_bytes >= 0);
    add_optional_long(j, "rx_packets", interfaces[i].rx_packets,
                      interfaces[i].rx_packets >= 0);
    add_optional_long(j, "tx_packets", interfaces[i].tx_packets,
                      interfaces[i].tx_packets >= 0);
    add_optional_long(j, "rx_errors", interfaces[i].rx_errors,
                      interfaces[i].rx_errors >= 0);
    add_optional_long(j, "tx_errors", interfaces[i].tx_errors,
                      interfaces[i].tx_errors >= 0);
    add_optional_long(j, "rx_dropped", interfaces[i].rx_dropped,
                      interfaces[i].rx_dropped >= 0);
    add_optional_long(j, "tx_dropped", interfaces[i].tx_dropped,
                      interfaces[i].tx_dropped >= 0);
    json_obj_close(j);
    json_obj_close(j);
  }
  json_arr_close(j);
  json_obj_close(j);
}

static int is_usb_device_name(const char *name) {
  const char *colon;
  if (!name || name[0] == '.') return 0;
  colon = strchr(name, ':');
  return colon == NULL;
}

static void emit_usb(JsonBuilder *j) {
  DIR *dir;
  struct dirent *entry;
  int count = 0;
  char text[256];

  json_key_obj_open(j, "usb");
  json_add_bool(j, "dwc3_present",
                path_exists("/sys/bus/platform/devices/29100000.usb3"));
  json_add_bool(j, "xhci_present",
                path_exists("/sys/bus/usb/devices/usb1") ||
                    path_exists("/sys/bus/usb/devices/usb2"));
  if (read_text("/sys/class/udc/29100000.dwc3/state", text, sizeof(text)) == 0) {
    json_add_str(j, "udc_state", text);
  } else {
    json_add_null(j, "udc_state");
  }

  json_key_obj_open(j, "typec");
  if (read_text("/sys/class/typec/port0/data_role", text, sizeof(text)) == 0) {
    json_add_str(j, "data_role", text);
  } else {
    json_add_null(j, "data_role");
  }
  if (read_text("/sys/class/typec/port0/power_role", text, sizeof(text)) == 0) {
    json_add_str(j, "power_role", text);
  } else {
    json_add_null(j, "power_role");
  }
  if (read_text("/sys/class/typec/port0/port_type", text, sizeof(text)) == 0) {
    json_add_str(j, "port_type", text);
  } else {
    json_add_null(j, "port_type");
  }
  if (read_text("/sys/class/typec/port0/vconn_source", text, sizeof(text)) == 0) {
    json_add_str(j, "vconn_source", text);
  } else {
    json_add_null(j, "vconn_source");
  }
  if (read_text("/sys/class/typec/port0-partner/accessory_mode", text,
                sizeof(text)) == 0) {
    json_add_str(j, "partner_accessory_mode", text);
  } else {
    json_add_null(j, "partner_accessory_mode");
  }
  json_add_bool(j, "partner_path_present",
                path_exists("/sys/class/typec/port0-partner"));
  json_add_bool(j, "vbus_voltage_measured", 0);
  json_obj_close(j);

  json_arr_open(j, "devices");
  dir = opendir("/sys/bus/usb/devices");
  if (dir) {
    while ((entry = readdir(dir)) != NULL && count < MAX_USB_DEVICES) {
      char base[HEALTH_PATH_SIZE];
      char driver_path[HEALTH_PATH_SIZE];
      char vendor_path[HEALTH_PATH_SIZE];
      char product_path[HEALTH_PATH_SIZE];
      char speed_path[HEALTH_PATH_SIZE];
      char manufacturer_path[HEALTH_PATH_SIZE];
      char vendor[64];
      char product_id[64];
      char product[256];
      char manufacturer[256];
      char driver[128];
      long long speed;

      if (!is_usb_device_name(entry->d_name)) continue;
      if (join_sysfs_name(base, sizeof(base), "/sys/bus/usb/devices/",
                          entry->d_name, "") != 0 ||
          join_path(vendor_path, sizeof(vendor_path), base, "/idVendor") != 0 ||
          join_path(product_path, sizeof(product_path), base, "/idProduct") != 0 ||
          join_path(speed_path, sizeof(speed_path), base, "/speed") != 0 ||
          join_path(manufacturer_path, sizeof(manufacturer_path), base,
                    "/manufacturer") != 0 ||
          join_path(driver_path, sizeof(driver_path), base, "/driver") != 0) {
        continue;
      }

      if (read_text(vendor_path, vendor, sizeof(vendor)) != 0) {
        if (strncmp(entry->d_name, "usb", 3) != 0) continue;
        copy_text(vendor, sizeof(vendor), "1d6b");
      }
      if (read_text(product_path, product_id, sizeof(product_id)) != 0) {
        copy_text(product_id, sizeof(product_id), "0000");
      }
      if (join_path(product_path, sizeof(product_path), base, "/product") != 0 ||
          read_text(product_path, product, sizeof(product)) != 0) {
        product[0] = '\0';
      }
      if (read_text(manufacturer_path, manufacturer, sizeof(manufacturer)) != 0) {
        manufacturer[0] = '\0';
      }
      if (read_link_name(driver_path, driver, sizeof(driver)) != 0) {
        driver[0] = '\0';
      }

      json_arr_obj_open(j);
      json_add_str(j, "path", entry->d_name);
      json_add_str(j, "vendor_id", vendor);
      json_add_str(j, "product_id", product_id);
      json_add_str(j, "manufacturer", manufacturer);
      json_add_str(j, "product", product);
      json_add_str(j, "driver", driver);
      if (read_number(speed_path, &speed) == 0) {
        json_add_long(j, "speed_mbps", speed);
      } else {
        json_add_null(j, "speed_mbps");
      }
      json_add_bool(j, "root_hub", strncmp(entry->d_name, "usb", 3) == 0);
      json_obj_close(j);
      count++;
    }
    closedir(dir);
  }
  json_arr_close(j);
  json_obj_close(j);
}

static void emit_wireless(JsonBuilder *j) {
  DIR *dir;
  struct dirent *entry;
  char vendor_path[PATH_MAX];
  char device_path[PATH_MAX];
  char driver_path[PATH_MAX];
  char vendor[64];
  char device[64];
  char driver[128];
  int found = 0;
  int bluetooth_count = 0;

  json_key_obj_open(j, "wireless");
  json_key_obj_open(j, "wcn");
  dir = opendir("/sys/bus/pci/devices");
  if (dir) {
    while ((entry = readdir(dir)) != NULL && !found) {
      snprintf(vendor_path, sizeof(vendor_path),
               "/sys/bus/pci/devices/%s/vendor", entry->d_name);
      snprintf(device_path, sizeof(device_path),
               "/sys/bus/pci/devices/%s/device", entry->d_name);
      if (read_text(vendor_path, vendor, sizeof(vendor)) != 0 ||
          read_text(device_path, device, sizeof(device)) != 0) {
        continue;
      }
      if (strcmp(vendor, "0x1db3") != 0 || strcmp(device, "0x5623") != 0) {
        continue;
      }
      snprintf(driver_path, sizeof(driver_path),
               "/sys/bus/pci/devices/%s/driver", entry->d_name);
      if (read_link_name(driver_path, driver, sizeof(driver)) != 0) {
        driver[0] = '\0';
      }
      json_add_bool(j, "present", 1);
      json_add_str(j, "vendor_id", vendor);
      json_add_str(j, "device_id", device);
      json_add_str(j, "driver", driver);
      json_add_str(j, "chip_family", "Unisoc 5623 / Marlin3");
      json_add_bool(j, "exact_model_confirmed", 0);
      json_add_str(j, "evidence_level", "probable");
      found = 1;
    }
    closedir(dir);
  }
  if (!found) {
    json_add_bool(j, "present", 0);
    json_add_null(j, "vendor_id");
    json_add_null(j, "device_id");
    json_add_null(j, "driver");
    json_add_null(j, "chip_family");
    json_add_bool(j, "exact_model_confirmed", 0);
    json_add_str(j, "evidence_level", "unavailable");
  }
  json_obj_close(j);

  dir = opendir("/sys/class/bluetooth");
  if (dir) {
    while ((entry = readdir(dir)) != NULL) {
      if (strncmp(entry->d_name, "hci", 3) == 0) bluetooth_count++;
    }
    closedir(dir);
  }
  json_key_obj_open(j, "bluetooth");
  json_add_bool(j, "hardware_capable", found);
  json_add_bool(j, "runtime_available", bluetooth_count > 0);
  json_add_int(j, "hci_count", bluetooth_count);
  json_add_bool(j, "hci0_present", path_exists("/sys/class/bluetooth/hci0"));
  json_add_str(j, "status", bluetooth_count > 0 ? "available" : "not_initialized");
  json_obj_close(j);
  json_obj_close(j);
}

static void emit_summary(JsonBuilder *j, const ThermalSensor *sensors,
                         int thermal_count,
                         StorageStat *storage, int storage_count,
                         int eth0_down, int network_count) {
  int i;
  int warning = 0;
  int critical = 0;
  int storage_warning = 0;
  int storage_critical = 0;

  for (i = 0; i < thermal_count; i++) {
    const char *status = thermal_status(&sensors[i]);
    if (strcmp(status, "critical") == 0) critical = 1;
    if (strcmp(status, "warning") == 0) warning = 1;
  }
  for (i = 0; i < storage_count; i++) {
    if (storage[i].use_percent >= 95.0) storage_critical = 1;
    else if (storage[i].use_percent >= 90.0) storage_warning = 1;
  }
  if (storage_critical) critical = 1;
  if (storage_warning) warning = 1;

  json_key_obj_open(j, "summary");
  if (critical) json_add_str(j, "status", "critical");
  else if (warning) json_add_str(j, "status", "warning");
  else if (thermal_count > 0 || storage_count > 0 || network_count > 0)
    json_add_str(j, "status", "ok");
  else
    json_add_str(j, "status", "unknown");
  json_arr_open(j, "issues");
  if (critical) {
    if (storage_critical) json_arr_add_str(j, "storage_usage_critical");
    for (i = 0; i < thermal_count; i++) {
      const char *status = thermal_status(&sensors[i]);
      if (strcmp(status, "critical") == 0) {
        json_arr_add_str(j, "thermal_temperature_critical");
        break;
      }
    }
  } else if (warning) {
    if (storage_warning) json_arr_add_str(j, "storage_usage_high");
    for (i = 0; i < thermal_count; i++) {
      const char *status = thermal_status(&sensors[i]);
      if (strcmp(status, "warning") == 0) {
        json_arr_add_str(j, "thermal_temperature_elevated");
        break;
      }
    }
  }
  if (eth0_down) json_arr_add_str(j, "eth0_carrier_down");
  json_arr_close(j);
  json_add_bool(j, "persistent_storage_used", 0);
  json_obj_close(j);
}

void handle_health(struct mg_connection *c, struct mg_http_message *hm) {
  ThermalSensor *thermal = NULL;
  StorageStat *storage = NULL;
  NetworkInterfaceHealth *network = NULL;
  int thermal_count;
  int storage_count;
  int network_count;
  int eth0_down;
  long long max_millidegrees;
  long long warning_millidegrees;
  long long hot_millidegrees;
  long long critical_millidegrees;
  JsonBuilder *j;

  HTTP_CHECK_GET(c, hm);

  thermal = calloc(MAX_THERMAL_SENSORS, sizeof(*thermal));
  storage = calloc(MAX_STORAGE_MOUNTS, sizeof(*storage));
  network = calloc(MAX_NETWORK_INTERFACES, sizeof(*network));
  if (!thermal || !storage || !network) {
    free(thermal);
    free(storage);
    free(network);
    HTTP_ERROR(c, 500, "Unable to allocate health snapshot");
    return;
  }

  thermal_count = collect_thermal(
      thermal, MAX_THERMAL_SENSORS, &max_millidegrees, &warning_millidegrees,
      &hot_millidegrees, &critical_millidegrees);
  storage_count = collect_storage(storage, MAX_STORAGE_MOUNTS);
  network_count = collect_network(network, MAX_NETWORK_INTERFACES, &eth0_down);

  j = json_new();
  if (!j) {
    free(thermal);
    free(storage);
    free(network);
    HTTP_ERROR(c, 500, "Unable to allocate health response");
    return;
  }

  json_obj_open(j);
  json_add_long(j, "timestamp", (long long)time(NULL));
  emit_summary(j, thermal, thermal_count, storage, storage_count, eth0_down,
               network_count);
  emit_thermal(j, thermal, thermal_count, max_millidegrees,
               warning_millidegrees, hot_millidegrees,
               critical_millidegrees);
  emit_storage(j, storage, storage_count);
  emit_network(j, network, network_count);
  emit_usb(j);
  emit_wireless(j);
  json_obj_close(j);

  HTTP_OK_NO_STORE_FREE(c, json_finish(j));
  free(thermal);
  free(storage);
  free(network);
}
