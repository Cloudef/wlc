#include "callback.h"
#include <stdlib.h>
#include <assert.h>

#include <wayland-server.h>

static void
wl_cb_callback_destructor(struct wl_resource *resource)
{
   assert(resource);
   struct wlc_callback *callback = wl_resource_get_user_data(resource);

   if (callback) {
      callback->resource = NULL;
      wlc_callback_free(callback);
   }
}

void
wlc_callback_implement(struct wlc_callback *callback)
{
   wl_resource_set_implementation(callback->resource, NULL, callback, wl_cb_callback_destructor);
}

void
wlc_callback_free(struct wlc_callback *callback)
{
   assert(callback);

   if (callback->resource) {
      wl_resource_set_user_data(callback->resource, NULL);
      wl_resource_destroy(callback->resource);
   }

   wl_list_remove(&callback->link);
   free(callback);
}

struct wlc_callback*
wlc_callback_new(struct wl_resource *resource)
{
   struct wlc_callback *callback;
   if (!(callback = calloc(1, sizeof(struct wlc_callback))))
      return NULL;

   callback->resource = resource;
   return callback;
}
