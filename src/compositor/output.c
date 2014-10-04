#include "output.h"
#include "compositor.h"

#include <stdlib.h>
#include <assert.h>

#include <wayland-server.h>

static void
wl_cb_output_resource_destructor(struct wl_resource *resource)
{
   wl_list_remove(wl_resource_get_link(resource));
}

static void
wl_output_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wlc_output *output = data;

   struct wl_resource *resource;
   if (!(resource = wl_resource_create(client, &wl_output_interface, version, id)))
      goto fail;

   wl_resource_set_implementation(resource, NULL, output, &wl_cb_output_resource_destructor);
   wl_list_insert(&output->resources, wl_resource_get_link(resource));

   // FIXME: need real data

   wl_output_send_geometry(resource, 1680, 1050,
       output->physical_width, output->physical_height, 0 /* subpixel */,
       "make", "model", WL_OUTPUT_TRANSFORM_NORMAL);

   if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
      wl_output_send_scale(resource, 1);

   wl_output_send_mode(resource, 0, 1680, 1050, 60);

   if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
      wl_output_send_done(resource);

   return;

fail:
   wl_client_post_no_memory(client);
}

void
wlc_output_free(struct wlc_output *output)
{
   assert(output);

   if (output->global)
      wl_global_destroy(output->global);

   free(output);
}

struct wlc_output*
wlc_output_new(struct wlc_compositor *compositor)
{
   struct wlc_output *output;
   if (!(output = calloc(1, sizeof(struct wlc_output))))
      goto fail;

   if (!(output->global = wl_global_create(compositor->display, &wl_output_interface, 2, output, &wl_output_bind)))
      goto fail;

   wl_list_init(&output->resources);
   return output;

fail:
   if (output)
      wlc_output_free(output);
   return NULL;
}
