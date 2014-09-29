#include "seat.h"
#include "wlc.h"
#include "client.h"
#include "pointer.h"
#include "keyboard.h"
#include "keymap.h"
#include "macros.h"

#include "compositor/compositor.h"
#include "compositor/view.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <wayland-server.h>

static void
wl_cb_pointer_set_cursor(struct wl_client *wl_client, struct wl_resource *resource, uint32_t serial, struct wl_resource *surface_resource, int32_t hotspot_x, int32_t hotspot_y)
{
   (void)wl_client, (void)resource, (void)serial, (void)hotspot_x, (void)hotspot_y;
   // struct wlc_pointer *pointer = wl_resource_get_user_data(resource);
   STUBL(resource);

   if (surface_resource) {
      // struct wlc_surface *surface = wl_resource_get_user_data(surface_resource);
      /* TODO: change pointer surface */
   }
}

static void
wl_cb_input_resource_release(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client;
   wl_resource_destroy(resource);
}

static const struct wl_pointer_interface wl_pointer_implementation = {
   wl_cb_pointer_set_cursor,
   wl_cb_input_resource_release
};

static void
wl_cb_pointer_client_destructor(struct wl_resource *resource)
{
   assert(resource);
   struct wlc_pointer *pointer = wl_resource_get_user_data(resource);
   wlc_pointer_remove_client_for_resource(pointer, resource);
}

static void
wl_cb_seat_get_pointer(struct wl_client *wl_client, struct wl_resource *resource, uint32_t id)
{
   struct wlc_seat *seat = wl_resource_get_user_data(resource);

   if (!seat->pointer)
      return;

   struct wlc_client *client;
   if (!(client = wlc_client_for_client_with_wl_client_in_list(wl_client, &seat->compositor->clients))) {
      wl_resource_post_error(resource, 1, "client was not found (out of memory?)");
      return;
   }

   struct wl_resource *pointer_resource;
   if (!(pointer_resource = wl_resource_create(wl_client, &wl_pointer_interface, wl_resource_get_version(resource), id))) {
      wl_client_post_no_memory(wl_client);
      return;
   }

   client->input[WLC_POINTER] = pointer_resource;
   wl_resource_set_implementation(pointer_resource, &wl_pointer_implementation, seat->pointer, wl_cb_pointer_client_destructor);
}

static const struct wl_keyboard_interface wl_keyboard_implementation = {
   wl_cb_input_resource_release,
};

static void
wl_cb_keyboard_client_destructor(struct wl_resource *resource)
{
   assert(resource);
   struct wlc_keyboard *keyboard = wl_resource_get_user_data(resource);
   wlc_keyboard_remove_client_for_resource(keyboard, resource);
}

static void
wl_cb_seat_get_keyboard(struct wl_client *wl_client, struct wl_resource *resource, uint32_t id)
{
   (void)wl_client;
   struct wlc_seat *seat = wl_resource_get_user_data(resource);

   if (!seat->keyboard)
      return;

   struct wlc_client *client;
   if (!(client = wlc_client_for_client_with_wl_client_in_list(wl_client, &seat->compositor->clients))) {
      wl_resource_post_error(resource, 1, "client was not found (out of memory?)");
      return;
   }

   struct wl_resource *keyboard_resource;
   if (!(keyboard_resource = wl_resource_create(wl_client, &wl_keyboard_interface, wl_resource_get_version(resource), id))) {
      wl_client_post_no_memory(wl_client);
      return;
   }

   client->input[WLC_KEYBOARD] = keyboard_resource;
   wl_resource_set_implementation(keyboard_resource, &wl_keyboard_implementation, seat->keyboard, wl_cb_keyboard_client_destructor);

   if (seat->keymap)
      wl_keyboard_send_keymap(keyboard_resource, seat->keymap->format, seat->keymap->fd, seat->keymap->size);

   if (seat->compositor->interface.keyboard.init) {
      struct wlc_view *view;
      wl_list_for_each_reverse(view, &seat->compositor->views, link) {
         if (view->client != client)
            continue;

         seat->compositor->interface.keyboard.init(seat->compositor, view);
         break;
      }
   }
}

static void
wl_cb_seat_get_touch(struct wl_client *wl_client, struct wl_resource *resource, uint32_t id)
{
   (void)wl_client, (void)id;
   STUB(resource);
}

static const struct wl_seat_interface wl_seat_implementation = {
   wl_cb_seat_get_pointer,
   wl_cb_seat_get_keyboard,
   wl_cb_seat_get_touch
};

static void
wl_seat_bind(struct wl_client *wl_client, void *data, unsigned int version, unsigned int id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create(wl_client, &wl_seat_interface, MIN(version, 4), id))) {
      wl_client_post_no_memory(wl_client);
      fprintf(stderr, "-!- failed create resource or bad version (%u > %u)\n", version, 4);
      return;
   }

   wl_resource_set_implementation(resource, &wl_seat_implementation, data, NULL);

   struct wlc_seat *seat = data;
   enum wl_seat_capability caps = 0;

   if (seat->pointer)
      caps |= WL_SEAT_CAPABILITY_POINTER;

   if (seat->keyboard)
      caps |= WL_SEAT_CAPABILITY_KEYBOARD;

#if 0
   if (seat->touch)
      caps |= WL_SEAT_CAPABILITY_TOUCH;
#endif

   wl_seat_send_capabilities(resource, caps);

   if (version >= 2)
      wl_seat_send_name(resource, "wlc-seat");
}

static void
seat_pointer_motion(struct wlc_seat *seat, int32_t x, int32_t y)
{
   if (!seat->pointer)
      return;

   if (seat->compositor->interface.pointer.motion &&
      !seat->compositor->interface.pointer.motion(seat->compositor, seat->pointer->focus, x, y))
      return;

   uint32_t serial = wl_display_next_serial(seat->compositor->display);
   uint32_t time = seat->compositor->api.get_time();
   wlc_pointer_motion(seat->pointer, serial, time, x, y);
}

static void
seat_pointer_button(struct wlc_seat *seat, uint32_t button, enum wl_pointer_button_state state)
{
   if (!seat->pointer)
      return;

   if (seat->compositor->interface.pointer.button &&
      !seat->compositor->interface.pointer.button(seat->compositor, seat->pointer->focus, button, state))
      return;

   uint32_t serial = wl_display_next_serial(seat->compositor->display);
   uint32_t time = seat->compositor->api.get_time();
   wlc_pointer_button(seat->pointer, serial, time, button, state);
}

static void
seat_keyboard_key(struct wlc_seat *seat, uint32_t key, enum wl_keyboard_key_state state)
{
   if (!seat->keyboard)
      return;

   static enum wlc_modifier_bit mod_bits[WLC_MOD_LAST] = {
      WLC_BIT_MOD_SHIFT,
      WLC_BIT_MOD_CAPS,
      WLC_BIT_MOD_CTRL,
      WLC_BIT_MOD_ALT,
      WLC_BIT_MOD_MOD2,
      WLC_BIT_MOD_MOD3,
      WLC_BIT_MOD_LOGO,
      WLC_BIT_MOD_MOD5,
   };

   static enum wlc_led_bit led_bits[WLC_LED_LAST] = {
      WLC_BIT_LED_NUM,
      WLC_BIT_LED_CAPS,
      WLC_BIT_LED_SCROLL,
   };

   uint32_t mods = 0, lookup = seat->keyboard->mods.depressed | seat->keyboard->mods.latched;
   for (int i = 0; i < WLC_MOD_LAST; ++i) {
      if (lookup & (1 << seat->keymap->mods[i]))
         mods |= mod_bits[i];
   }

   uint32_t leds = 0;
   for (int i = 0; i < WLC_LED_LAST; ++i) {
      if (xkb_state_led_index_is_active(seat->keyboard->state, seat->keymap->leds[i]))
         leds |= led_bits[i];
   }

   if (seat->compositor->interface.keyboard.key &&
      !seat->compositor->interface.keyboard.key(seat->compositor, seat->keyboard->focus, leds, mods, key, state))
      return;

   uint32_t serial = wl_display_next_serial(seat->compositor->display);
   uint32_t time = seat->compositor->api.get_time();
   wlc_keyboard_key(seat->keyboard, serial, time, key, state);
}

static void
seat_keyboard_focus(struct wlc_seat *seat, struct wlc_view *view)
{
   if (!seat->keyboard)
      return;

   uint32_t serial = wl_display_next_serial(seat->compositor->display);
   wlc_keyboard_focus(seat->keyboard, serial, view);
}

void
wlc_seat_free(struct wlc_seat *seat)
{
   assert(seat);

   if (seat->global)
      wl_global_destroy(seat->global);

   if (seat->keyboard)
      wlc_keyboard_free(seat->keyboard);

   if (seat->pointer)
      wlc_pointer_free(seat->pointer);

   if (seat->keymap)
      wlc_keymap_free(seat->keymap);

   free(seat);
}

struct wlc_seat*
wlc_seat_new(struct wlc_compositor *compositor)
{
   struct wlc_seat *seat;
   if (!(seat = calloc(1, sizeof(struct wlc_seat))))
      goto out_of_memory;

   seat->pointer = wlc_pointer_new(&compositor->clients, &compositor->views);

   if ((seat->keymap = wlc_keymap_new(NULL, 0)))
      seat->keyboard = wlc_keyboard_new(seat->keymap, &compositor->clients, &compositor->views);

   if (!(seat->global = wl_global_create(compositor->display, &wl_seat_interface, 4, seat, wl_seat_bind)))
      goto shell_interface_fail;

   seat->notify.pointer_motion = seat_pointer_motion;
   seat->notify.pointer_button = seat_pointer_button;
   seat->notify.keyboard_key = seat_keyboard_key;
   seat->notify.keyboard_focus = seat_keyboard_focus;

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
