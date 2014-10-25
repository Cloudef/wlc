#ifndef _WLC_VIEW_H_
#define _WLC_VIEW_H_

#include <wayland-util.h>
#include <stdbool.h>

#include "wlc.h"
#include "shell/surface.h"
#include "shell/xdg-surface.h"
#include "shell/xdg-popup.h"
#include "types/geometry.h"

struct wl_resource;
struct wlc_client;
struct wlc_surface;
struct wlc_x11_window;

enum wlc_view_ack {
   ACK_NONE,
   ACK_PENDING,
   ACK_NEXT_COMMIT
};

struct wlc_view_state {
   struct wlc_geometry geometry;
   struct wlc_geometry visible;
   uint32_t state;
};

struct wlc_view {
   void *userdata;
   struct wlc_compositor *compositor;
   struct wlc_view *parent;
   struct wlc_client *client;
   struct wlc_space *space;
   struct wlc_surface *surface;
   struct wlc_x11_window *x11_window;
   struct wlc_shell_surface shell_surface;
   struct wlc_xdg_surface xdg_surface;
   struct wlc_xdg_popup xdg_popup;
   struct wl_list childs;
   struct wl_list link, user_link, parent_link;
   struct wlc_view_state pending;
   struct wlc_view_state commit;
   struct wl_array wl_state;
   uint32_t type;
   uint32_t resizing;
   enum wlc_view_ack ack;
   bool created;
};

void wlc_view_request_state(struct wlc_view *view, enum wlc_view_state_bit state, bool toggle);
void wlc_view_commit_state(struct wlc_view *view, struct wlc_view_state *pending, struct wlc_view_state *out);
void wlc_view_ack_surface_attach(struct wlc_view *view, struct wlc_size *old_surface_size);
void wlc_view_get_bounds(struct wlc_view *view, struct wlc_geometry *out_geometry);
void wlc_view_set_parent(struct wlc_view *view, struct wlc_view *parent);
struct wlc_space* wlc_view_get_mapped_space(struct wlc_view *view);
void wlc_view_free(struct wlc_view *view);
struct wlc_view* wlc_view_new(struct wlc_compositor *compositor, struct wlc_client *client, struct wlc_surface *surface);

#endif /* _WLC_VIEW_H_ */
