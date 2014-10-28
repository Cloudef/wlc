#include "keyboard.h"
#include "keymap.h"
#include "client.h"

#include "compositor/view.h"
#include "compositor/output.h"
#include "compositor/surface.h"
#include "compositor/compositor.h"

#include "xwayland/xwm.h"

#include <stdlib.h>
#include <assert.h>

#include <wayland-server.h>

static bool
is_valid_view(struct wlc_view *view)
{
   return (view && view->client && view->client->input[WLC_KEYBOARD] && view->surface && view->surface->resource);
}

static void
update_modifiers(struct wlc_keyboard *keyboard)
{
   assert(keyboard);
   keyboard->mods.depressed = xkb_state_serialize_mods(keyboard->state, XKB_STATE_DEPRESSED);
   keyboard->mods.latched = xkb_state_serialize_mods(keyboard->state, XKB_STATE_LATCHED);
   keyboard->mods.locked = xkb_state_serialize_mods(keyboard->state, XKB_STATE_LOCKED);
   keyboard->mods.group = xkb_state_serialize_layout(keyboard->state, XKB_STATE_LAYOUT_EFFECTIVE);
}

static bool
update_keys(struct wl_array *keys, uint32_t key, enum wl_keyboard_key_state state)
{
   assert(keys);

   uint32_t *k, *end = keys->data + keys->size;
   for (k = keys->data; k < end; ++k) {
      if (*k != key)
         continue;

      if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
         return false;

      *k = *--end;
      break;
   }

   keys->size = (void*)end - keys->data;
   if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
      if (!(k = wl_array_add(keys, sizeof(uint32_t))))
         return false;

      *k = key;
   }

   return true;
}

static void
send_release_for_keys(struct wlc_view *view, struct wl_array *keys)
{
   assert(view && keys);

   uint32_t *k;
   uint32_t time = view->compositor->api.get_time();
   uint32_t serial = wl_display_next_serial(view->compositor->display);
   wl_array_for_each(k, keys)
      wl_keyboard_send_key(view->client->input[WLC_KEYBOARD], serial, time, *k, WL_KEYBOARD_KEY_STATE_RELEASED);
}

void
reset_keyboard(struct wlc_keyboard *keyboard)
{
   assert(keyboard);

   if (is_valid_view(keyboard->focus))
      send_release_for_keys(keyboard->focus, &keyboard->keys);

   wl_array_release(&keyboard->keys);
   wl_array_init(&keyboard->keys);
}

bool
wlc_keyboard_update(struct wlc_keyboard *keyboard, uint32_t key, enum wl_keyboard_key_state state)
{
   if (!update_keys(&keyboard->keys, key, state))
      return false;

   xkb_state_update_key(keyboard->state, key + 8, (state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP));
   update_modifiers(keyboard);
   return true;
}

void
wlc_keyboard_key(struct wlc_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, enum wl_keyboard_key_state state)
{
   assert(keyboard);

   if (!is_valid_view(keyboard->focus))
      return;

   wl_keyboard_send_modifiers(keyboard->focus->client->input[WLC_KEYBOARD], serial, keyboard->mods.depressed, keyboard->mods.latched, keyboard->mods.locked, keyboard->mods.group);
   wl_keyboard_send_key(keyboard->focus->client->input[WLC_KEYBOARD], serial, time, key, state);
}

void
wlc_keyboard_focus(struct wlc_keyboard *keyboard, uint32_t serial, struct wlc_view *view)
{
   assert(keyboard);

   // Do not allow focusing of override_redirect (X11) views.
   // This will cause some popups to immediately close. (Chromium)
   if (view && (view->type & WLC_BIT_OVERRIDE_REDIRECT))
      return;

   if (keyboard->focus == view)
      return;

   if (is_valid_view(keyboard->focus)) {
      send_release_for_keys(keyboard->focus, &keyboard->keys);
      wl_keyboard_send_leave(keyboard->focus->client->input[WLC_KEYBOARD], serial, keyboard->focus->surface->resource);

      if (keyboard->focus->xdg_popup.resource)
         wlc_view_close(keyboard->focus);

      if (keyboard->focus->x11_window)
         wlc_x11_window_set_active(keyboard->focus->x11_window, false);
   }

   if (is_valid_view(view)) {
      if (view->x11_window)
         wlc_x11_window_set_active(view->x11_window, true);

      wl_keyboard_send_enter(view->client->input[WLC_KEYBOARD], serial, view->surface->resource, &keyboard->keys);
   }

   keyboard->focus = view;
}

void
wlc_keyboard_remove_client_for_resource(struct wlc_keyboard *keyboard, struct wl_resource *resource)
{
   assert(keyboard && resource);

   // FIXME: this is hack (see also pointer.c)
   // We could
   // a) Use destroy listeners on resource.
   // b) Fix wlc resource management to pools and handles, so we immediately know if resource is valid or not.
   //    This is also safer against misbehaving clients, and simpler API.

   struct wlc_output *output;
   wl_list_for_each(output, &keyboard->compositor->outputs, link) {
      struct wlc_space *space;
      wl_list_for_each(space, &output->spaces, link) {
         struct wlc_view *view;
         wl_list_for_each(view, &space->views, link) {
            if (view->client->input[WLC_KEYBOARD] != resource)
               continue;

            if (keyboard->focus && keyboard->focus->client && keyboard->focus->client->input[WLC_KEYBOARD] == resource) {
               view->client->input[WLC_KEYBOARD] = NULL;
               wlc_keyboard_focus(keyboard, 0, NULL);
            } else {
               view->client->input[WLC_KEYBOARD] = NULL;
            }

            return;
         }
      }
   }
}

bool
wlc_keyboard_set_keymap(struct wlc_keyboard *keyboard, struct wlc_keymap *keymap)
{
   assert(keyboard);

   if (!keymap && keyboard->state)
      xkb_state_unref(keyboard->state);

   if (keymap && (!(keyboard->state = xkb_state_new(keymap->keymap))))
      return false;

   return true;
}

void
wlc_keyboard_free(struct wlc_keyboard *keyboard)
{
   assert(keyboard);

   if (keyboard->compositor) {
      struct wlc_client *client;
      wl_list_for_each(client, &keyboard->compositor->clients, link)
         client->input[WLC_KEYBOARD] = NULL;
   }

   if (keyboard->state)
      xkb_state_unref(keyboard->state);

   wl_array_release(&keyboard->keys);
   free(keyboard);
}

struct wlc_keyboard*
wlc_keyboard_new(struct wlc_keymap *keymap, struct wlc_compositor *compositor)
{
   assert(keymap && compositor);

   struct wlc_keyboard *keyboard;
   if (!(keyboard = calloc(1, sizeof(struct wlc_keyboard))))
      goto fail;

   if (!wlc_keyboard_set_keymap(keyboard, keymap))
      goto fail;

   keyboard->compositor = compositor;
   wl_array_init(&keyboard->keys);
   return keyboard;

fail:
   if (keyboard)
      wlc_keyboard_free(keyboard);
   return NULL;
}
