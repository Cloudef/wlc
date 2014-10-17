#include "buffer.h"
#include <stdlib.h>
#include <assert.h>

#include <wayland-server.h>

void
wlc_buffer_free(struct wlc_buffer *buffer)
{
   assert(buffer);

   if (buffer->references > 0 && --buffer->references > 0)
      return;

   if (buffer->resource)
      wl_resource_queue_event(buffer->resource, WL_BUFFER_RELEASE);

   free(buffer);
}

struct wlc_buffer*
wlc_buffer_use(struct wlc_buffer *buffer)
{
   if (!buffer)
      return NULL;

   buffer->references++;
   return buffer;
}

struct wlc_buffer*
wlc_buffer_new(struct wl_resource *resource)
{
   struct wlc_buffer *buffer;
   if (!(buffer = calloc(1, sizeof(struct wlc_buffer))))
      return NULL;

   buffer->resource = resource;
   buffer->y_inverted = true;
   return buffer;
}
