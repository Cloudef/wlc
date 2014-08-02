#ifndef _WLC_XDG_SURFACE_H_
#define _WLC_XDG_SURFACE_H_

#include "types/string.h"
#include "compositor/surface.h"
#include <stdbool.h>

struct wl_resource;
struct wlc_surface;
struct wlc_shell_surface;

struct wlc_geometry {
   int32_t x, y, w, h;
};

struct wlc_xdg_surface {
   struct wlc_shell_surface *shell_surface;
   struct wlc_geometry visible_geometry;
   struct wlc_string app_id;
   bool minimized;
};

void wlc_xdg_surface_implement(struct wlc_xdg_surface *xdg_surface, struct wl_resource *resource);
void wlc_xdg_surface_set_app_id(struct wlc_xdg_surface *xdg_surface, const char *app_id);
void wlc_xdg_surface_set_minimized(struct wlc_xdg_surface *xdg_surface, bool minimized);
void wlc_xdg_surface_free(struct wlc_xdg_surface *xdg_surface);
struct wlc_xdg_surface* wlc_xdg_surface_new(struct wlc_surface *surface);

#endif /* _WLC_XDG_SURFACE_H_ */
