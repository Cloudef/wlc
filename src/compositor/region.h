#ifndef _WLC_REGION_H_
#define _WLC_REGION_H_

#include <wayland-server.h>
#include <pixman.h>

struct wlc_region {
   struct wl_resource *resource;
   pixman_region32_t region;
};

void wlc_region_implement(struct wlc_region *region, struct wl_resource *resource);
void wlc_region_free(struct wlc_region *region);
struct wlc_region* wlc_region_new(void);

#endif /* _WLC_REGION_H_ */
