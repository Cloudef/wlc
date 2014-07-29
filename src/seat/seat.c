#include "seat.h"
#include "macros.h"

#include "compositor/compositor.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <wayland-server.h>

static void
wl_cb_seat_get_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   (void)client, (void)id;
   STUB(resource);
}

static void
wl_cb_seat_get_keyboard(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   (void)client, (void)id;
   STUB(resource);
}

static void
wl_cb_seat_get_touch(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   (void)client, (void)id;
   STUB(resource);
}

static const struct wl_seat_interface wl_seat_implementation = {
   wl_cb_seat_get_pointer,
   wl_cb_seat_get_keyboard,
   wl_cb_seat_get_touch
};

static void
wl_seat_bind(struct wl_client *client, void *data, unsigned int version, unsigned int id)
{
   (void)data;

   struct wl_resource *resource;
   if (!(resource = wl_resource_create(client, &wl_seat_interface, MIN(version, 3), id))) {
      wl_client_post_no_memory(client);
      fprintf(stderr, "-!- failed create resource or bad version (%u > %u)", version, 3);
      return;
   }

   wl_resource_set_implementation(resource, &wl_seat_implementation, data, NULL);

   /* TODO: implement these caps
    * wl_seat_send_capabilities(resource, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    */

   if (version >= 2)
      wl_seat_send_name(resource, "fixme");
}

void
wlc_seat_free(struct wlc_seat *seat)
{
   assert(seat);

   if (seat->global)
      wl_global_destroy(seat->global);

   free(seat);
}

struct wlc_seat*
wlc_seat_new(struct wlc_compositor *compositor)
{
   struct wlc_seat *seat;
   if (!(seat = calloc(1, sizeof(struct wlc_seat))))
      goto out_of_memory;

   if (!(seat->global = wl_global_create(compositor->display, &wl_seat_interface, 3, seat, wl_seat_bind)))
      goto shell_interface_fail;

   return seat;

out_of_memory:
   fprintf(stderr, "-!- out of memory\n");
   goto fail;
shell_interface_fail:
   fprintf(stderr, "-!- failed to bind shell interface\n");
fail:
   if (seat)
      wlc_seat_free(seat);
   return NULL;
}
