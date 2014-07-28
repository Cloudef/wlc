#ifndef _WLC_SURFACE_H_
#define _WLC_SURFACE_H_

#include <wayland-server.h>
#include <pixman.h>
#include <stdbool.h>

struct wlc_surface_state {
   struct wlc_buffer *buffer;
   pixman_region32_t opaque;
   pixman_region32_t input;
   pixman_region32_t damage;
   int32_t x, y;
   bool newly_attached;
};

struct wlc_surface {
   struct wl_resource *resource;
   struct wlc_callback *frame_cb;
   struct wlc_surface_state pending;
   struct wlc_surface_state commit;
   int32_t width, height;
   int32_t width_from_buffer, height_from_buffer;
   int32_t ref_count;
};

void wlc_surface_implement(struct wlc_surface *surface, struct wl_resource *resource);
struct wlc_surface* wlc_surface_ref(struct wlc_surface *surface);
void wlc_surface_release(struct wlc_surface *surface);
struct wlc_surface* wlc_surface_new(void);

#endif /* _WLC_SURFACE_H_ */
