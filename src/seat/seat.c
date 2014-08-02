#include "seat.h"
#include "pointer.h"
#include "keyboard.h"
#include "keymap.h"
#include "macros.h"

#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "backend/backend.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <wayland-server.h>

static void
wl_cb_pointer_set_cursor(struct wl_client *client, struct wl_resource *resource, uint32_t serial, struct wl_resource *surface_resource, int32_t hotspot_x, int32_t hotspot_y)
{
   (void)client, (void)resource, (void)serial, (void)hotspot_x, (void)hotspot_y;
   // struct wlc_pointer *pointer = wl_resource_get_user_data(resource);
   STUBL(resource);

   if (surface_resource) {
      // struct wlc_surface *surface = wl_resource_get_user_data(surface_resource);
      /* TODO: change pointer surface */
   }
}

static void
wl_cb_pointer_release(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;
   struct wlc_pointer *pointer = wl_resource_get_user_data(resource);

   if (pointer->focus == resource) {
      pointer->focus = NULL;
      pointer->grabbing = false;
      pointer->action = WLC_GRAB_ACTION_NONE;
      pointer->action_edges = 0;
   }

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
wl_cb_keyboard_release(struct wl_client *client, struct wl_resource *resource)
{
   (void)client;
   struct wlc_keyboard *keyboard = wl_resource_get_user_data(resource);

   if (keyboard->focus == resource) {
      keyboard->focus = NULL;
   }

   wl_resource_destroy(resource);
}

static const struct wl_keyboard_interface wl_keyboard_implementation = {
   wl_cb_keyboard_release,
};

static void
wl_cb_keyboard_client_destructor(struct wl_resource *resource)
{
   assert(resource);
   wl_list_remove(wl_resource_get_link(resource));
}

static void
wl_cb_seat_get_keyboard(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   (void)client;
   struct wlc_seat *seat = wl_resource_get_user_data(resource);

   if (!seat->keyboard)
      return;

   struct wl_resource *keyboard_resource;
   if (!(keyboard_resource = wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(resource), id))) {
      wl_client_post_no_memory(client);
      return;
   }

   wl_list_insert(&seat->keyboard->resource_list, wl_resource_get_link(keyboard_resource));
   wl_resource_set_implementation(keyboard_resource, &wl_keyboard_implementation, seat->keyboard, wl_cb_keyboard_client_destructor);

   if (seat->keymap)
      wl_keyboard_send_keymap(keyboard_resource, seat->keymap->format, seat->keymap->fd, seat->keymap->size);
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

   if (seat->keyboard)
      caps |= WL_SEAT_CAPABILITY_KEYBOARD;

#if 0
   if (seat->touch)
      caps |= WL_SEAT_CAPABILITY_TOUCH;
#endif

   wl_seat_send_capabilities(resource, caps);

   if (version >= 2)
      wl_seat_send_name(resource, "wlc seat");
}

static void
keyboard_focus(struct wlc_seat *seat, struct wlc_surface *surface)
{
   if (!seat->keyboard)
      return;

   if (!surface) {
      seat->keyboard->focus = NULL;
      return;
   }

   struct wl_resource *r;
   wl_list_for_each(r, &seat->keyboard->resource_list, link) {
      if (wl_resource_get_client(r) != wl_resource_get_client(surface->resource))
         continue;

      if (r == seat->keyboard->focus)
         return;

      if (seat->keyboard->focus)
         wl_keyboard_send_leave(r, wl_display_next_serial(seat->compositor->display), surface->resource);

      struct wl_array keys;
      wl_array_init(&keys);
      wl_keyboard_send_enter(r, wl_display_next_serial(seat->compositor->display), surface->resource, &keys);
      break;
   }

   seat->keyboard->focus = r;
}

static void
pointer_focus(struct wlc_seat *seat, struct wlc_surface *surface, wl_fixed_t fx, wl_fixed_t fy)
{
   keyboard_focus(seat, surface);

   if (!seat->pointer)
      return;

   if (!surface) {
      seat->pointer->focus = NULL;
      seat->pointer->grabbing = false;
      seat->pointer->action = WLC_GRAB_ACTION_NONE;
      seat->pointer->action_edges = 0;
      return;
   }

   struct wl_resource *r;
   wl_list_for_each(r, &seat->pointer->resource_list, link) {
      if (wl_resource_get_client(r) != wl_resource_get_client(surface->resource))
         continue;

      if (r == seat->pointer->focus)
         return;

      if (seat->pointer->focus)
         wl_pointer_send_leave(r, wl_display_next_serial(seat->compositor->display), surface->resource);
      wl_pointer_send_enter(r, wl_display_next_serial(seat->compositor->display), surface->resource, fx, fy);
      break;
   }

   seat->pointer->focus = r;
   seat->pointer->grabbing = false;
   seat->pointer->action = WLC_GRAB_ACTION_NONE;
   seat->pointer->action_edges = 0;
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
      if (x >= surface->geometry.x && x <= surface->geometry.x + surface->geometry.w &&
          y >= surface->geometry.y && y <= surface->geometry.y + surface->geometry.h) {
         focused = surface;
         break;
      }
   }

   if (focused) {
      struct wl_resource *r;
      uint32_t msec = seat->compositor->api.get_time();
      wl_fixed_t fx = wl_fixed_from_int(x - focused->geometry.x);
      wl_fixed_t fy = wl_fixed_from_int(y - focused->geometry.y);

      pointer_focus(seat, focused, fx, fy);
      wl_list_for_each(r, &seat->pointer->resource_list, link) {
         if (wl_resource_get_client(r) != wl_resource_get_client(focused->resource))
            continue;

         wl_pointer_send_motion(r, msec, fx, fy);

         if (seat->pointer->grabbing) {
            int32_t dx = x - wl_fixed_to_int(seat->pointer->gx);
            int32_t dy = y - wl_fixed_to_int(seat->pointer->gy);

            if (seat->pointer->action == WLC_GRAB_ACTION_MOVE) {
               focused->geometry.x += dx;
               focused->geometry.y += dy;
            } else if (seat->pointer->action == WLC_GRAB_ACTION_RESIZE) {
               if (seat->pointer->action_edges & WL_SHELL_SURFACE_RESIZE_LEFT) {
                  focused->geometry.w -= dx;
                  focused->geometry.x += dx;
               } else if (seat->pointer->action_edges & WL_SHELL_SURFACE_RESIZE_RIGHT) {
                  focused->geometry.w += dx;
               }

               if (seat->pointer->action_edges & WL_SHELL_SURFACE_RESIZE_TOP) {
                  focused->geometry.h -= dy;
                  focused->geometry.y += dy;
               } else if (seat->pointer->action_edges & WL_SHELL_SURFACE_RESIZE_BOTTOM) {
                  focused->geometry.h += dy;
               }
            }

            seat->pointer->gx = seat->pointer->x;
            seat->pointer->gy = seat->pointer->y;
         }
         break;
      }
   } else {
      pointer_focus(seat, NULL, 0, 0);
   }
}

static void
pointer_button(struct wlc_seat *seat, uint32_t button, enum wl_pointer_button_state state)
{
   if (!seat->pointer || !seat->pointer->focus)
      return;

   if (state == WL_POINTER_BUTTON_STATE_PRESSED && !seat->pointer->grabbing) {
      seat->pointer->grabbing = true;
      seat->pointer->gx = seat->pointer->x;
      seat->pointer->gy = seat->pointer->y;
   } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
      seat->pointer->grabbing = false;
      seat->pointer->action = WLC_GRAB_ACTION_NONE;
      seat->pointer->action_edges = 0;
   }

   wl_pointer_send_button(seat->pointer->focus, wl_display_next_serial(seat->compositor->display), seat->compositor->api.get_time(), button, state);
}

static void
keyboard_modifiers(struct wlc_seat *seat)
{
   if (!seat->keyboard)
      return;

   uint32_t depressed = xkb_state_serialize_mods(seat->keyboard->state, XKB_STATE_DEPRESSED);
   uint32_t latched = xkb_state_serialize_mods(seat->keyboard->state, XKB_STATE_LATCHED);
   uint32_t locked = xkb_state_serialize_mods(seat->keyboard->state, XKB_STATE_LOCKED);
   uint32_t group = xkb_state_serialize_group(seat->keyboard->state, XKB_STATE_EFFECTIVE);

   if (depressed == seat->keyboard->mods.depressed &&
       latched == seat->keyboard->mods.latched &&
       locked == seat->keyboard->mods.locked &&
       group == seat->keyboard->mods.group)
      return;

   seat->keyboard->mods.depressed = depressed;
   seat->keyboard->mods.latched = latched;
   seat->keyboard->mods.locked = locked;
   seat->keyboard->mods.group = group;

   if (!seat->keyboard->focus)
      return;

   wl_keyboard_send_modifiers(seat->keyboard->focus, wl_display_next_serial(seat->compositor->display), depressed, latched, locked, group);
}

static void
keyboard_key(struct wlc_seat *seat, uint32_t key, uint32_t state)
{
   if (!seat->keyboard)
      return;

   xkb_state_update_key(seat->keyboard->state, key + 8, (state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP));

   if (!seat->keyboard->focus)
      return;

   keyboard_modifiers(seat);
   wl_keyboard_send_key(seat->keyboard->focus, wl_display_next_serial(seat->compositor->display), seat->compositor->api.get_time(), key, state);
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

   seat->pointer = wlc_pointer_new();

   {
      struct xkb_rule_names names;
      memset(&names, 0, sizeof(names));

      if ((seat->keymap = wlc_keymap_new(&names, 0)))
         seat->keyboard = wlc_keyboard_new(seat->keymap);
   }

   if (!(seat->global = wl_global_create(compositor->display, &wl_seat_interface, 3, seat, wl_seat_bind)))
      goto shell_interface_fail;

   seat->notify.pointer_motion = pointer_motion;
   seat->notify.pointer_button = pointer_button;
   seat->notify.keyboard_key = keyboard_key;

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
