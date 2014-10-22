#ifndef _WLC_XDG_SURFACE_H_
#define _WLC_XDG_SURFACE_H_

#include <stdbool.h>

#include "types/string.h"
#include "types/geometry.h"

struct wl_resource;
struct wlc_view;

enum wlc_xdg_surface_ack {
   XDG_ACK_NONE,
   XDG_ACK_PENDING,
   XDG_ACK_NEXT_COMMIT
};

// Inherted by wlc_view
struct wlc_xdg_surface {
   struct wl_resource *resource;
   struct wlc_string app_id;
   enum wlc_xdg_surface_ack ack;
   bool minimized;
};

void wlc_xdg_surface_implement(struct wlc_xdg_surface *xdg_surface, struct wlc_view *view, struct wl_resource *resource);
void wlc_xdg_surface_set_app_id(struct wlc_xdg_surface *xdg_surface, const char *app_id);
void wlc_xdg_surface_set_minimized(struct wlc_xdg_surface *xdg_surface, bool minimized);
void wlc_xdg_surface_release(struct wlc_xdg_surface *xdg_surface);

#endif /* _WLC_XDG_SURFACE_H_ */
