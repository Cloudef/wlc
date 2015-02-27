#ifndef _WLC_DATA_DEVICE_MANAGER_H_
#define _WLC_DATA_DEVICE_MANAGER_H_

#include <stdbool.h>
#include <wayland-server.h>
#include "resources/resources.h"

struct wl_global;

struct wlc_data_device_manager {
   struct wlc_source sources, devices, offers;

   struct {
      struct wl_global *manager;
   } wl;

   wlc_resource source;
};

void wlc_data_device_manager_offer(struct wlc_data_device_manager *device, struct wl_client *client);
void wlc_data_device_manager_release(struct wlc_data_device_manager *manager);
bool wlc_data_device_manager(struct wlc_data_device_manager *manager);

#endif /* _WLC_DATA_DEVICE_MANAGER_H_ */
