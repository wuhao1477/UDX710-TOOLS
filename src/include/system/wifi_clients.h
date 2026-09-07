#ifndef WIFI_CLIENTS_H
#define WIFI_CLIENTS_H

#include "wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

int wifi_client_parse_arp_line(const char *line, WifiClient *client);
int wifi_client_parse_neigh_line(const char *line, WifiClient *client);
int wifi_client_parse_fdb_line(const char *line, char *mac, size_t mac_size,
                               int *port, int *local);
int wifi_client_parse_bridge_port(const char *line, char *interface_name,
                                  size_t interface_size, int *port);
const char *wifi_client_access_type(const char *interface_name);

#ifdef __cplusplus
}
#endif

#endif
