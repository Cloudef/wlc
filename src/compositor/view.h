#ifndef _WLC_VIEW_H_
#define _WLC_VIEW_H_

#include <wayland-util.h>
#include <stdbool.h>

#include "wlc.h"
#include "types/geometry.h"

struct wl_resource;
struct wlc_client;
struct wlc_surface;
struct wlc_shell_surface;
struct wlc_xdg_surface;
struct wlc_x11_window;

struct wlc_view_state {
   struct wlc_geometry geometry;
   uint32_t state;
};

struct wlc_view {
   void *userdata;
   struct wlc_client *client;
   struct wlc_surface *surface;
   struct wlc_shell_surface *shell_surface; // XXX: join into wlc_view ?
   struct wlc_xdg_surface *xdg_surface; // XXX: join into wlc_view ?
   struct wlc_x11_window *x11_window;
   struct wl_list link, user_link;
   struct wlc_view_state pending;
   struct wlc_view_state commit;
   struct wl_array wl_state;
};

void wlc_view_commit_state(struct wlc_view *view, struct wlc_view_state *pending, struct wlc_view_state *out);
void wlc_view_get_bounds(struct wlc_view *view, struct wlc_geometry *out_geometry);
struct wlc_view* wlc_view_for_surface_in_list(struct wlc_surface *surface, struct wl_list *list);
void wlc_view_set_xdg_surface(struct wlc_view *view, struct wlc_xdg_surface *xdg_surface);
void wlc_view_set_shell_surface(struct wlc_view *view, struct wlc_shell_surface *shell_surface);
void wlc_view_free(struct wlc_view *view);
struct wlc_view* wlc_view_new(struct wlc_client *client, struct wlc_surface *compositor);
void wlc_view_position(struct wlc_view *view, int32_t x, int32_t y);
void wlc_view_resize(struct wlc_view *view, uint32_t width, uint32_t height);
void wlc_view_set_maximized(struct wlc_view *view, bool maximized);
void wlc_view_set_fullscreen(struct wlc_view *view, bool fullscreen);

#endif /* _WLC_VIEW_H_ */
