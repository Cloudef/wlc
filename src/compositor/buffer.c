#include "buffer.h"
#include <stdlib.h>
#include <assert.h>

void
wlc_buffer_free(struct wlc_buffer *buffer)
{
   assert(buffer);
   free(buffer);
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
