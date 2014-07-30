#ifndef _WLC_POINTER_H_
#define _WLC_POINTER_H_

#include <stdint.h>
#include <stdbool.h>
#include <wayland-util.h>

enum grab_action {
   WLC_GRAB_ACTION_NONE,
   WLC_GRAB_ACTION_MOVE,
   WLC_GRAB_ACTION_RESIZE
};

struct wlc_pointer {
   struct wl_list resource_list;
   struct wl_resource *focus;
   wl_fixed_t x, y;

   wl_fixed_t gx, gy;
   uint32_t action_edges;
   enum grab_action action;
   bool grabbing;
};

void wlc_pointer_free(struct wlc_pointer *pointer);
struct wlc_pointer* wlc_pointer_new(void);

#endif /* _WLC_POINTER_H_ */
