#ifndef _WLC_XDG_POSITIONER_H_
#define _WLC_XDG_POSITIONER_H_

#include <wayland-server.h>
#include "wayland-xdg-shell-unstable-v6-server-protocol.h"

const struct zxdg_positioner_v6_interface* wlc_xdg_positioner_implementation(void);

struct wlc_xdg_positioner {
   // struct weston_desktop *desktop;
   struct wl_client *client;
   // wlc_resource resource;
   // struct weston_size size;
   // struct weston_geometry anchor_rect;
   
   // enum zxdg_positioner_v6_anchor anchor;
   // enum zxdg_positioner_v6_gravity gravity;
   // enum zxdg_positioner_v6_constraint_adjustment constraint_adjustment;
   // struct weston_position offset;
};

#endif /* _WLC_XDG_POSITIONER_H_ */
