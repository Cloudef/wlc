#ifndef _WLC_REGION_H_
#define _WLC_REGION_H_

#include <pixman.h>
#include <stdbool.h>

struct wlc_region {
   pixman_region32_t region;
};

void wlc_region_release(struct wlc_region *region);

const struct wl_region_interface* wlc_region_implementation(void);

#endif /* _WLC_REGION_H_ */
