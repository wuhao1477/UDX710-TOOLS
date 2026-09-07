#include <assert.h>
#include <string.h>

#include "wifi_clients.h"

static void test_arp_client(void) {
  WifiClient client;
  assert(wifi_client_parse_arp_line(
             "192.168.0.2 0x1 0x2 46:91:dd:92:a6:54 * tether", &client) ==
         0);
  assert(strcmp(client.mac, "46:91:dd:92:a6:54") == 0);
  assert(strcmp(client.ipv4, "192.168.0.2") == 0);
  assert(strcmp(client.interface, "tether") == 0);
}

static void test_ipv6_neighbor(void) {
  WifiClient client;
  assert(wifi_client_parse_neigh_line(
             "fe80::1 dev tether lladdr 46:91:dd:92:a6:54 REACHABLE",
             &client) == 0);
  assert(strcmp(client.mac, "46:91:dd:92:a6:54") == 0);
  assert(strcmp(client.ipv6, "fe80::1") == 0);
}

static void test_bridge_port_classification(void) {
  char interface_name[32];
  int port = 0;
  char mac[18];
  int local = 0;

  assert(wifi_client_parse_bridge_port("wlan0 (1)", interface_name,
                                      sizeof(interface_name), &port) == 0);
  assert(strcmp(interface_name, "wlan0") == 0 && port == 1);
  assert(wifi_client_parse_fdb_line(
             "  1 46:91:dd:92:a6:54 no 0.06", mac, sizeof(mac), &port,
             &local) == 0);
  assert(strcmp(mac, "46:91:dd:92:a6:54") == 0 && port == 1 && !local);
  assert(strcmp(wifi_client_access_type("wlan0"), "wifi") == 0);
  assert(strcmp(wifi_client_access_type("usb0"), "usb") == 0);
  assert(strcmp(wifi_client_access_type("eth0"), "rj45") == 0);
}

int main(void) {
  test_arp_client();
  test_ipv6_neighbor();
  test_bridge_port_classification();
  return 0;
}
