#include "buffer.h"
#include <stdlib.h>
#include <assert.h>

void
wlc_buffer_dispose(struct wlc_buffer *buffer)
{
   if (!buffer)
      return;

   if (buffer->references && --buffer->references > 0)
      return;

   wlc_resource_release(convert_to_wlc_resource(buffer));
}

wlc_resource
wlc_buffer_use(struct wlc_buffer *buffer)
{
   if (!buffer)
      return 0;

   buffer->references++;
   return convert_to_wlc_resource(buffer);
}

void
wlc_buffer_release(struct wlc_buffer *buffer)
{
   struct wl_resource *resource;
   if ((resource = convert_to_wl_resource(buffer, "buffer"))) {
      wlc_resource_invalidate(convert_to_wlc_resource(buffer));
      wl_resource_queue_event(resource, WL_BUFFER_RELEASE);
   }
}

bool
wlc_buffer(struct wlc_buffer *buffer)
{
   assert(buffer);
   buffer->y_inverted = true;
   return true;
}
