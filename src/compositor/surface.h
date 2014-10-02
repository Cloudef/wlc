#ifndef _WLC_SURFACE_H_
#define _WLC_SURFACE_H_

#include <wayland-util.h>
#include <pixman.h>
#include <stdbool.h>

struct wl_resource;
struct wlc_buffer;
struct wlc_compositor;
struct wlc_callback;
struct wlc_surface_state;

struct wlc_surface_state {
   struct wlc_buffer *buffer;
   pixman_region32_t opaque;
   pixman_region32_t input;
   pixman_region32_t damage;
   int32_t sx, sy;
   bool newly_attached;
};

struct wlc_surface {
   struct wl_resource *resource;
   struct wlc_compositor *compositor;
   struct wlc_surface_state pending;
   struct wlc_surface_state commit;
   struct wl_list frame_cb_list;

   /**
    * "Texture" as we use OpenGL terminology, but can be id to anything.
    * Managed by the renderer.
    */
   uint32_t texture;

   int32_t width, height;
   bool created;
};

void wlc_surface_create_notify(struct wlc_surface *surface);
void wlc_surface_implement(struct wlc_surface *surface, struct wl_resource *resource);
void wlc_surface_free(struct wlc_surface *surface);
struct wlc_surface* wlc_surface_new(struct wlc_compositor *compositor);

#endif /* _WLC_SURFACE_H_ */
