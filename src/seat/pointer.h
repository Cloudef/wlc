#ifndef _WLC_POINTER_H_
#define _WLC_POINTER_H_

#include <stdint.h>
#include <wayland-util.h>

struct wlc_pointer {
   struct wl_list resource_list;
   struct wl_resource *focus;
   wl_fixed_t x, y;
};

void wlc_pointer_free(struct wlc_pointer *pointer);
struct wlc_pointer* wlc_pointer_new(void);

#endif /* _WLC_POINTER_H_ */
