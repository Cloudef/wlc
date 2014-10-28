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

   uint32_t depressed = xkb_state_serialize_mods(keyboard->state, XKB_STATE_DEPRESSED);
   uint32_t latched = xkb_state_serialize_mods(keyboard->state, XKB_STATE_LATCHED);
   uint32_t locked = xkb_state_serialize_mods(keyboard->state, XKB_STATE_LOCKED);
   uint32_t group = xkb_state_serialize_group(keyboard->state, XKB_STATE_EFFECTIVE);

   if (depressed == keyboard->mods.depressed &&
       latched == keyboard->mods.latched &&
       locked == keyboard->mods.locked &&
       group == keyboard->mods.group)
      return;

   keyboard->mods.depressed = depressed;
   keyboard->mods.latched = latched;
   keyboard->mods.locked = locked;
   keyboard->mods.group = group;
}

void
wlc_keyboard_update(struct wlc_keyboard *keyboard, uint32_t key, enum wl_keyboard_key_state state)
{
   xkb_state_update_key(keyboard->state, key + 8, (state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP));
   update_modifiers(keyboard);
}

void
wlc_keyboard_key(struct wlc_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, enum wl_keyboard_key_state state)
{
   assert(keyboard);

   if (!is_valid_view(keyboard->focus))
      return;

   wl_keyboard_send_key(keyboard->focus->client->input[WLC_KEYBOARD], serial, time, key, state);
   wl_keyboard_send_modifiers(keyboard->focus->client->input[WLC_KEYBOARD], serial,
         keyboard->mods.depressed, keyboard->mods.latched, keyboard->mods.locked, keyboard->mods.group);
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
      wl_keyboard_send_leave(keyboard->focus->client->input[WLC_KEYBOARD], serial, keyboard->focus->surface->resource);

      if (keyboard->focus->xdg_popup.resource)
         wlc_view_close(keyboard->focus);

      if (keyboard->focus->x11_window)
         wlc_x11_window_set_active(keyboard->focus->x11_window, false);
   }

   if (is_valid_view(view)) {
      if (view->x11_window)
         wlc_x11_window_set_active(view->x11_window, true);

      struct wl_array keys;
      wl_array_init(&keys);
      wl_keyboard_send_enter(view->client->input[WLC_KEYBOARD], serial, view->surface->resource, &keys);
      wl_keyboard_send_modifiers(view->client->input[WLC_KEYBOARD], serial,
            keyboard->mods.depressed, keyboard->mods.latched, keyboard->mods.locked, keyboard->mods.group);
      wl_array_release(&keys);
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
   return keyboard;

fail:
   if (keyboard)
      wlc_keyboard_free(keyboard);
   return NULL;
}
