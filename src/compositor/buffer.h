#ifndef _WLC_BUFFER_H_
#define _WLC_BUFFER_H_

#include <stdbool.h>
#include <stdint.h>

#include <wayland-server.h>

struct wl_resource;
struct wl_shm_buffer;

struct wlc_buffer {
   struct wl_resource *resource;
   struct wl_listener destroy_listener;

   union {
      struct wl_shm_buffer *shm_buffer;
      void *legacy_buffer;
   };

   uint32_t references;
   int32_t width, height;
   bool y_inverted;
};

struct wlc_buffer* wlc_buffer_resource_get_container(struct wl_resource *buffer_resource);

void wlc_buffer_free(struct wlc_buffer *buffer);
struct wlc_buffer* wlc_buffer_use(struct wlc_buffer *buffer);
struct wlc_buffer* wlc_buffer_new(struct wl_resource *resource);

#endif /* _WLC_BUFFER_H_ */
