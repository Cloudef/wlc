#include "seat.h"
#include "pointer.h"
#include "macros.h"

#include "compositor/compositor.h"
#include "compositor/surface.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <wayland-server.h>

static void
wl_cb_pointer_set_cursor(struct wl_client *client, struct wl_resource *resource, uint32_t serial, struct wl_resource *surface_resource, int32_t hotspot_x, int32_t hotspot_y)
{
   (void)client, (void)serial, (void)hotspot_x, (void)hotspot_y;
   // struct wlc_pointer *pointer = wl_resource_get_user_data(resource);
   STUBL(resource);

   if (surface_resource) {
      struct wlc_surface *surface = wl_resource_get_user_data(surface_resource);
      /* TODO: change pointer surface */
   }
}

static void
wl_cb_pointer_release(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;
   struct wlc_pointer *pointer = wl_resource_get_user_data(resource);

   if (pointer->focus == resource)
      pointer->focus = NULL;

   wl_resource_destroy(resource);
}

static const struct wl_pointer_interface wl_pointer_implementation = {
   wl_cb_pointer_set_cursor,
   wl_cb_pointer_release
};

static void
wl_cb_pointer_client_destructor(struct wl_resource *resource)
{
   assert(resource);
   wl_list_remove(wl_resource_get_link(resource));
}

static void
wl_cb_seat_get_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   (void)client;
   struct wlc_seat *seat = wl_resource_get_user_data(resource);

   if (!seat->pointer)
      return;

   struct wl_resource *pointer_resource;
   if (!(pointer_resource = wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(resource), id))) {
      wl_client_post_no_memory(client);
      return;
   }

   wl_list_insert(&seat->pointer->resource_list, wl_resource_get_link(pointer_resource));
   wl_resource_set_implementation(pointer_resource, &wl_pointer_implementation, seat->pointer, wl_cb_pointer_client_destructor);
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
   struct wl_resource *resource;
   if (!(resource = wl_resource_create(client, &wl_seat_interface, MIN(version, 3), id))) {
      wl_client_post_no_memory(client);
      fprintf(stderr, "-!- failed create resource or bad version (%u > %u)", version, 3);
      return;
   }

   wl_resource_set_implementation(resource, &wl_seat_implementation, data, NULL);

   struct wlc_seat *seat = data;
   enum wl_seat_capability caps = 0;

   if (seat->pointer)
      caps |= WL_SEAT_CAPABILITY_POINTER;

#if 0
   if (seat->keyboard)
      caps |= WL_SEAT_CAPABILITY_KEYBOARD;

   if (seat->touch)
      caps |= WL_SEAT_CAPABILITY_TOUCH;
#endif

   wl_seat_send_capabilities(resource, caps);

   if (version >= 2)
      wl_seat_send_name(resource, "wlc seat");
}

static void
pointer_motion(struct wlc_seat *seat, int32_t x, int32_t y)
{
   if (!seat->pointer)
      return;

   seat->pointer->x = wl_fixed_from_int(x);
   seat->pointer->y = wl_fixed_from_int(y);

   struct wlc_surface *surface, *focused = NULL;
   wl_list_for_each(surface, &seat->compositor->surfaces, link) {
      if (x >= surface->commit.x && x <= surface->commit.x + surface->width &&
          y >= surface->commit.y && y <= surface->commit.y + surface->height) {
         focused = surface;
         break;
      }
   }

   if (focused) {
      struct wl_resource *r;
      uint32_t msec = seat->compositor->api.get_time();
      wl_list_for_each(r, &seat->pointer->resource_list, link) {
         if (wl_resource_get_client(r) == wl_resource_get_client(focused->resource)) {
            if (r != seat->pointer->focus) {
               if (seat->pointer->focus)
                  wl_pointer_send_leave(r, wl_display_next_serial(seat->compositor->display), focused->resource);
               wl_pointer_send_enter(r, wl_display_next_serial(seat->compositor->display), focused->resource, seat->pointer->x, seat->pointer->y);
               seat->pointer->focus = r;
            }
            wl_pointer_send_motion(r, msec, seat->pointer->x, seat->pointer->y);
            break;
         }
      }
   } else if (focused != seat->pointer->focus) {
      seat->pointer->focus = NULL;
   }
}

static void
pointer_button(struct wlc_seat *seat, uint32_t button, enum wl_pointer_button_state state)
{
   if (!seat->pointer || !seat->pointer->focus)
      return;

   wl_pointer_send_button(seat->pointer->focus, wl_display_next_serial(seat->compositor->display), seat->compositor->api.get_time(), button, state);
}

void
wlc_seat_free(struct wlc_seat *seat)
{
   assert(seat);

   if (seat->global)
      wl_global_destroy(seat->global);

   if (seat->pointer)
      wlc_pointer_free(seat->pointer);

   free(seat);
}

struct wlc_seat*
wlc_seat_new(struct wlc_compositor *compositor)
{
   struct wlc_seat *seat;
   if (!(seat = calloc(1, sizeof(struct wlc_seat))))
      goto out_of_memory;

   seat->pointer = wlc_pointer_new();

   if (!(seat->global = wl_global_create(compositor->display, &wl_seat_interface, 3, seat, wl_seat_bind)))
      goto shell_interface_fail;

   seat->notify.pointer_motion = pointer_motion;
   seat->notify.pointer_button = pointer_button;

   seat->compositor = compositor;
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
