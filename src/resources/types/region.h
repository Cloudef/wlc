#ifndef _WLC_REGION_H_
#define _WLC_REGION_H_

#include <pixman.h>
#include <stdbool.h>

struct wlc_region {
   pixman_region32_t region;
};

const struct wl_region_interface wl_region_implementation;

void wlc_region_release(struct wlc_region *region);

#endif /* _WLC_REGION_H_ */
