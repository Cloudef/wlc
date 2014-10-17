#include "buffer.h"
#include <stdlib.h>
#include <assert.h>

#include <wayland-server.h>

#define container_of(ptr, type, member) ({                     \
      const __typeof__( ((type *)0)->member ) *__mptr = (ptr); \
      (type *)( (char *)__mptr - offsetof(type,member) );})

static void
wl_cb_buffer_destroy_cb(struct wl_listener *listener, void *data)
{
   (void)data;
   struct wlc_buffer *buffer = container_of(listener, struct wlc_buffer, destroy_listener);
   wl_list_remove(&buffer->destroy_listener.link);
   buffer->resource = NULL;
}

struct wlc_buffer*
wlc_buffer_resource_get_container(struct wl_resource *buffer_resource)
{
   struct wl_listener *listener;
   if (!(listener = wl_resource_get_destroy_listener(buffer_resource, wl_cb_buffer_destroy_cb)))
      return NULL;

   return container_of(listener, struct wlc_buffer, destroy_listener);
}

void
wlc_buffer_free(struct wlc_buffer *buffer)
{
   assert(buffer);

   if (buffer->references > 0 && --buffer->references > 0)
      return;

   if (buffer->resource) {
      wl_list_remove(&buffer->destroy_listener.link);
      wl_resource_queue_event(buffer->resource, WL_BUFFER_RELEASE);
   }

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

   buffer->destroy_listener.notify = wl_cb_buffer_destroy_cb;
   wl_resource_add_destroy_listener(resource, &buffer->destroy_listener);

   buffer->resource = resource;
   buffer->y_inverted = true;
   return buffer;
}
