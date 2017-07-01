#ifndef _WLC_DATA_DEVICE_MANAGER_H_
#define _WLC_DATA_DEVICE_MANAGER_H_

#include <stdbool.h>
#include <wayland-server.h>
#include "resources/resources.h"
#include "resources/types/data-source.h"

struct wl_global;

struct wlc_data_device_manager {
   struct wlc_source sources, devices, offers;

   struct {
      struct wl_global *manager;
   } wl;

   struct wlc_data_source *source;
};

WLC_NONULLV(1) void wlc_data_device_manager_offer(struct wlc_data_device_manager *device, struct wl_client *client);
void wlc_data_device_manager_release(struct wlc_data_device_manager *manager);
WLC_NONULL bool wlc_data_device_manager(struct wlc_data_device_manager *manager);

void wlc_data_device_manager_set_source(struct wlc_data_device_manager *manager, struct wlc_data_source *source);
void wlc_data_device_manager_set_custom_selection(struct wlc_data_device_manager *manager, void *data,
        const char *const *types, size_t types_count, void (*send)(void *data, const char *type, int fd));

#endif /* _WLC_DATA_DEVICE_MANAGER_H_ */
