#include "output.h"
#include "visibility.h"
#include "compositor.h"

#include <stdlib.h>
#include <string.h>
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

   wl_output_send_geometry(resource, output->information.x, output->information.y,
       output->information.physical_width, output->information.physical_height, output->information.subpixel,
       (output->information.make.data ? output->information.make.data : "unknown"),
       (output->information.model.data ? output->information.model.data : "model"),
       output->information.transform);

   if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
      wl_output_send_scale(resource, output->information.scale);

   bool no_current = true;
   struct wlc_output_mode *mode;
   wl_array_for_each(mode, &output->information.modes) {
      wl_output_send_mode(resource, mode->flags, mode->width, mode->height, mode->refresh);

      if (mode->flags & WL_OUTPUT_MODE_CURRENT)
         no_current = false;
   }

   assert(!no_current && "output should have at least one current mode!");

   if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
      wl_output_send_done(resource);

   return;

fail:
   wl_client_post_no_memory(client);
}

bool
wlc_output_information_add_mode(struct wlc_output_information *info, struct wlc_output_mode *mode)
{
   assert(info && mode);

   struct wlc_output_mode *copied;
   if (!(copied = wl_array_add(&info->modes, sizeof(struct wlc_output_mode))))
      return false;

   memcpy(copied, mode, sizeof(struct wlc_output_mode));
   return true;
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
wlc_output_new(struct wlc_compositor *compositor, struct wlc_output_information *info)
{
   struct wlc_output *output;
   if (!(output = calloc(1, sizeof(struct wlc_output))))
      goto fail;

   if (!(output->global = wl_global_create(compositor->display, &wl_output_interface, 2, output, &wl_output_bind)))
      goto fail;

   memcpy(&output->information, info, sizeof(output->information));
   wl_list_init(&output->resources);
   wl_list_init(&output->views);
   return output;

fail:
   if (output)
      wlc_output_free(output);
   return NULL;
}

WLC_API void
wlc_output_get_resolution(struct wlc_output *output, uint32_t *out_width, uint32_t *out_height)
{
   assert(output);

   if (out_width)
      *out_width = output->resolution.width;

   if (out_height)
      *out_height = output->resolution.height;
}

WLC_API struct wl_list*
wlc_output_get_views(struct wlc_output *output)
{
   assert(output);
   return &output->views;
}

WLC_API void
wlc_output_set_userdata(struct wlc_output *output, void *userdata)
{
   assert(output);
   output->userdata = userdata;
}

WLC_API void*
wlc_output_get_userdata(struct wlc_output *output)
{
   assert(output);
   return output->userdata;
}
