#ifndef _WLC_DATA_DEVICE_MANAGER_H_
#define _WLC_DATA_DEVICE_MANAGER_H_

#include <stdint.h>
#include <wayland-util.h>
#include <wayland-server.h>

struct wl_global;
struct wl_client;
struct wlc_compositor;

struct wlc_data_device {
   struct wl_resource *source_resource;
   struct wl_listener source_resource_listener;
   struct wl_list resources;
};

struct wlc_data_device_manager {
   struct wl_global *global;
   struct wlc_compositor *compositor;
};

void wlc_data_device_offer(struct wlc_data_device *device, struct wl_client *wl_client);
struct wlc_data_device* wlc_data_device_new(void);
void wlc_data_device_free(struct wlc_data_device *device);
void wlc_data_device_manager_free(struct wlc_data_device_manager *manager);
struct wlc_data_device_manager* wlc_data_device_manager_new(struct wlc_compositor *compositor);

#endif /* _WLC_DATA_DEVICE_MANAGER_H_ */
