#ifndef _WLC_DATA_DEVICE_MANAGER_H_
#define _WLC_DATA_DEVICE_MANAGER_H_

#include <stdint.h>

struct wl_global;
struct wlc_compositor;

struct wlc_data_device_manager {
   struct wl_global *global;
   struct wlc_compositor *compositor;
};

void wlc_data_device_manager_free(struct wlc_data_device_manager *manager);
struct wlc_data_device_manager* wlc_data_device_manager_new(struct wlc_compositor *compositor);

#endif /* _WLC_DATA_DEVICE_MANAGER_H_ */
