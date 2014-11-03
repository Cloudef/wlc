#ifndef _WLC_SURFACE_H_
#define _WLC_SURFACE_H_

#include <wayland-util.h>
#include <pixman.h>

#include "types/geometry.h"

struct wl_resource;
struct wlc_buffer;
struct wlc_callback;
struct wlc_surface_state;
struct wlc_render;
struct wlc_output;
struct wlc_view;

struct wlc_surface_state {
   struct wlc_buffer *buffer;
   struct wl_list frame_cb_list;
   pixman_region32_t opaque;
   pixman_region32_t input;
   pixman_region32_t damage;
   struct wlc_origin offset;
   int32_t scale;
   bool attached;
};

struct wlc_surface {
   struct wl_resource *resource;
   struct wlc_surface_state pending;
   struct wlc_surface_state commit;
   struct wlc_size size;

   /* Set if this surface is bind to view */
   struct wlc_view *view;

   /* Current output the surface is attached to */
   struct wlc_output *output;

   /**
    * "Texture" as we use OpenGL terminology, but can be id to anything.
    * Managed by the renderer.
    */
   uint32_t textures[3];

   /**
    * Images, contains hw surfaces that can be anything (For example EGL KHR Images in EGL/gles2 renderer).
    * Managed by the renderer.
    */
   void *images[3];

   enum wlc_surface_format {
      SURFACE_RGB,
      SURFACE_RGBA,
   } format;

   bool opaque;
};

void wlc_surface_attach_to_output(struct wlc_surface *surface, struct wlc_output *output, struct wlc_buffer *buffer);
void wlc_surface_invalidate(struct wlc_surface *surface);
void wlc_surface_implement(struct wlc_surface *surface, struct wl_resource *resource);
void wlc_surface_free(struct wlc_surface *surface);
struct wlc_surface* wlc_surface_new(void);

#endif /* _WLC_SURFACE_H_ */
