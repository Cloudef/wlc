#ifndef _WLC_SURFACE_H_
#define _WLC_SURFACE_H_

#include <stdbool.h>
#include <wayland-util.h>
#include <pixman.h>
#include <wlc/geometry.h>
#include <chck/pool/pool.h>
#include "resources/resources.h"

struct wlc_buffer;
struct wlc_output;
struct wlc_view;

struct wlc_surface_state {
   struct chck_iter_pool frame_cbs;
   pixman_region32_t opaque;
   pixman_region32_t input;
   pixman_region32_t damage;
   struct wlc_origin offset;
   wlc_resource buffer;
   int32_t scale;
   bool attached;
};

struct wlc_surface {
   struct wlc_source buffers, callbacks;
   struct wlc_surface_state pending;
   struct wlc_surface_state commit;
   struct wlc_size size;

   /* Parent surface for subsurface interface */
   wlc_resource parent;

   /* Set if this surface is bind to view */
   wlc_resource view;

   /* Current output the surface is attached to */
   wlc_resource output;

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

   bool synchronized;
};

const struct wl_surface_interface wl_surface_implementation;

struct wlc_buffer* wlc_surface_get_buffer(struct wlc_surface *surface);
void wlc_surface_attach_to_view(struct wlc_surface *surface, struct wlc_view *view);
bool wlc_surface_attach_to_output(struct wlc_surface *surface, struct wlc_output *output, struct wlc_buffer *buffer);
void wlc_surface_set_parent(struct wlc_surface *surface, struct wlc_surface *parent);
void wlc_surface_invalidate(struct wlc_surface *surface);
void wlc_surface_release(struct wlc_surface *surface);
bool wlc_surface(struct wlc_surface *surface);

#endif /* _WLC_SURFACE_H_ */
