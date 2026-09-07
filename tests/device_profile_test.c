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

static void test_link_state_parsing(void) {
  assert(device_profile_parse_link_state("1", "up") == 1);
  assert(device_profile_parse_link_state("0", "down") == 0);
  assert(device_profile_parse_link_state("", "up") == 1);
  assert(device_profile_parse_link_state("0", "unknown") == 0);
}

int main(void) {
  test_hostapd_wlan1();
  test_led_fallback();
  test_data_interface_priority();
  test_current_typec_rndis();
  test_unsupported_typec_switch();
  test_link_state_parsing();
  return 0;
}
