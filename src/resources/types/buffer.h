#ifndef _WLC_BUFFER_H_
#define _WLC_BUFFER_H_

#include <wayland-server.h>
#include <wlc/geometry.h>
#include "resources/resources.h"

struct wl_shm_buffer;

struct wlc_buffer {
   struct wlc_size size;
   wlc_resource surface;

   union {
      struct wl_shm_buffer *shm_buffer;
      void *legacy_buffer;
   };

   uint16_t references;
   bool y_inverted;
};

void wlc_buffer_dispose(struct wlc_buffer *buffer);
wlc_resource wlc_buffer_use(struct wlc_buffer *buffer);
void wlc_buffer_release(struct wlc_buffer *buffer);
WLC_NONULL bool wlc_buffer(struct wlc_buffer *buffer);

#endif /* _WLC_BUFFER_H_ */
