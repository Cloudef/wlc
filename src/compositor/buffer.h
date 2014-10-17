#ifndef _WLC_BUFFER_H_
#define _WLC_BUFFER_H_

#include <wayland-server.h>

#include "types/geometry.h"

struct wl_resource;
struct wl_shm_buffer;

struct wlc_buffer {
   struct wl_resource *resource;
   struct wl_listener destroy_listener;
   struct wlc_size size;

   union {
      struct wl_shm_buffer *shm_buffer;
      void *legacy_buffer;
   };

   uint32_t references;
   bool y_inverted;
};

struct wlc_buffer* wlc_buffer_resource_get_container(struct wl_resource *buffer_resource);

void wlc_buffer_free(struct wlc_buffer *buffer);
struct wlc_buffer* wlc_buffer_use(struct wlc_buffer *buffer);
struct wlc_buffer* wlc_buffer_new(struct wl_resource *resource);

#endif /* _WLC_BUFFER_H_ */
