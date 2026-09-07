#define _POSIX_C_SOURCE 200809L

#include "wifi_clients.h"

#include <stdio.h>
#include <string.h>

static int valid_mac(const char *mac) {
  if (!mac || strlen(mac) != 17) {
    return 0;
  }
  for (size_t i = 0; i < 17; i++) {
    if (i == 2 || i == 5 || i == 8 || i == 11 || i == 14) {
      if (mac[i] != ':') {
        return 0;
      }
    } else if (!((mac[i] >= '0' && mac[i] <= '9') ||
                 (mac[i] >= 'a' && mac[i] <= 'f') ||
                 (mac[i] >= 'A' && mac[i] <= 'F'))) {
      return 0;
    }
  }
  return 1;
}

const char *wifi_client_access_type(const char *interface_name) {
  if (!interface_name) {
    return "unknown";
  }
  if (strncmp(interface_name, "wlan", 4) == 0) {
    return "wifi";
  }
  if (strncmp(interface_name, "usb", 3) == 0 ||
      strncmp(interface_name, "sipa_usb", 8) == 0) {
    return "usb";
  }
  if (strncmp(interface_name, "eth", 3) == 0 ||
      strncmp(interface_name, "en", 2) == 0 ||
      strncmp(interface_name, "lan", 3) == 0) {
    return "rj45";
  }
  if (strcmp(interface_name, "tether") == 0) {
    return "bridge";
  }
  return "unknown";
}

static void set_interface(WifiClient *client, const char *interface_name) {
  snprintf(client->interface, sizeof(client->interface), "%s",
           interface_name ? interface_name : "");
  snprintf(client->access_type, sizeof(client->access_type), "%s",
           wifi_client_access_type(interface_name));
}

int wifi_client_parse_arp_line(const char *line, WifiClient *client) {
  char ip[40];
  char hardware[16];
  char flags[16];
  char mac[32];
  char mask[40];
  char interface_name[32];

  if (!line || !client ||
      sscanf(line, "%39s %15s %15s %31s %39s %31s", ip, hardware, flags,
             mac, mask, interface_name) != 6 || !valid_mac(mac)) {
    return -1;
  }
  snprintf(client->mac, sizeof(client->mac), "%s", mac);
  snprintf(client->ipv4, sizeof(client->ipv4), "%s", ip);
  set_interface(client, interface_name);
  return 0;
}

int wifi_client_parse_neigh_line(const char *line, WifiClient *client) {
  char copy[256];
  char *save = NULL;
  char *token;
  char *ip = NULL;
  char *interface_name = NULL;
  char *mac = NULL;

  if (!line || !client || strlen(line) >= sizeof(copy)) {
    return -1;
  }
  snprintf(copy, sizeof(copy), "%s", line);
  token = strtok_r(copy, " \t\r\n", &save);
  if (!token) {
    return -1;
  }
  ip = token;
  while ((token = strtok_r(NULL, " \t\r\n", &save)) != NULL) {
    if (strcmp(token, "dev") == 0) {
      interface_name = strtok_r(NULL, " \t\r\n", &save);
    } else if (strcmp(token, "lladdr") == 0) {
      mac = strtok_r(NULL, " \t\r\n", &save);
    }
  }
  if (!mac || !valid_mac(mac)) {
    return -1;
  }
  snprintf(client->mac, sizeof(client->mac), "%s", mac);
  if (strchr(ip, ':')) {
    snprintf(client->ipv6, sizeof(client->ipv6), "%s", ip);
  } else {
    snprintf(client->ipv4, sizeof(client->ipv4), "%s", ip);
  }
  set_interface(client, interface_name ? interface_name : "tether");
  return 0;
}

int wifi_client_parse_fdb_line(const char *line, char *mac, size_t mac_size,
                               int *port, int *local) {
  char local_text[8];
  double age;
  char parsed_mac[32];
  int parsed_port;

  if (!line || !mac || mac_size == 0 || !port || !local ||
      sscanf(line, " %d %31s %7s %lf", &parsed_port, parsed_mac, local_text,
             &age) < 3 || !valid_mac(parsed_mac)) {
    return -1;
  }
  if (strlen(parsed_mac) >= mac_size) {
    return -1;
  }
  snprintf(mac, mac_size, "%s", parsed_mac);
  *port = parsed_port;
  *local = strcmp(local_text, "yes") == 0;
  return 0;
}

int wifi_client_parse_bridge_port(const char *line, char *interface_name,
                                  size_t interface_size, int *port) {
  char parsed_interface[32];
  int parsed_port;

  if (!line || !interface_name || interface_size == 0 || !port ||
      sscanf(line, " %31s (%d)", parsed_interface, &parsed_port) != 2 ||
      strlen(parsed_interface) >= interface_size) {
    return -1;
  }
  snprintf(interface_name, interface_size, "%s", parsed_interface);
  *port = parsed_port;
  return 0;
}
