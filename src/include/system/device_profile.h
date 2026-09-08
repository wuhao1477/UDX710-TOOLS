#ifndef DEVICE_PROFILE_H
#define DEVICE_PROFILE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  DEVICE_USB_UNKNOWN = 0,
  DEVICE_USB_RNDIS_CURRENT = 1,
  DEVICE_USB_SWITCHABLE_GENERIC = 2
} DeviceUsbMode;

typedef struct {
  char model[128];
  int mains_powered;
  int battery_supported;
  int rj45_physical_present;
  int rj45_interface_present;
  int rj45_link_up;
  int rj45_usable;
  int typec_present;
  int typec_host_capable;
  int usb_mode_switch_supported;
  int usb_rndis_available;
  int usb_rndis_link_up;
  char wifi_iface[32];
  char wifi_config[256];
  char data_iface[32];
  char rj45_iface[32];
  char led_red[256];
  char led_green[256];
  char led_blue[256];
  char usb_vid[16];
  char usb_pid[16];
  char usb_rndis_iface[32];
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
int device_profile_parse_link_state(const char *carrier, const char *operstate);
int device_profile_select_rndis_iface(const char *const *names, size_t count,
                                      char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
