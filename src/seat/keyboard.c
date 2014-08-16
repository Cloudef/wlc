#include "keyboard.h"
#include "keymap.h"
#include "client.h"

#include "compositor/view.h"
#include "compositor/surface.h"

#include <stdlib.h>
#include <assert.h>

#include <wayland-server.h>

static void
update_modifiers(struct wlc_keyboard *keyboard, uint32_t serial)
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

   if (!keyboard->focus || !keyboard->focus->client->input[WLC_KEYBOARD])
      return;

   wl_keyboard_send_modifiers(keyboard->focus->client->input[WLC_KEYBOARD], serial, depressed, latched, locked, group);
}

void
wlc_keyboard_key(struct wlc_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, enum wl_keyboard_key_state state)
{
   assert(keyboard);

   xkb_state_update_key(keyboard->state, key + 8, (state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP));
   update_modifiers(keyboard, serial);

   if (!keyboard->focus || !keyboard->focus->client->input[WLC_KEYBOARD])
      return;

   wl_keyboard_send_key(keyboard->focus->client->input[WLC_KEYBOARD], serial, time, key, state);
}

void
wlc_keyboard_focus(struct wlc_keyboard *keyboard, uint32_t serial, struct wlc_view *view)
{
   assert(keyboard);

   if (keyboard->focus == view)
      return;

   if (!view) {
      if (keyboard->focus)
         keyboard->focus = NULL;
      return;
   }

   if (keyboard->focus && keyboard->focus->client->input[WLC_KEYBOARD])
      wl_keyboard_send_leave(keyboard->focus->client->input[WLC_KEYBOARD], serial, keyboard->focus->surface->resource);

   if (view->client->input[WLC_KEYBOARD]) {
      struct wl_array keys;
      wl_array_init(&keys);

      wl_keyboard_send_enter(view->client->input[WLC_KEYBOARD], serial, view->surface->resource, &keys);
      keyboard->focus = view;
   } else {
      keyboard->focus = NULL;
   }
}

void
wlc_keyboard_remove_client_for_resource(struct wlc_keyboard *keyboard, struct wl_resource *resource)
{
   assert(keyboard && resource);

   struct wlc_view *view;
   wl_list_for_each(view, keyboard->views, link) {
      if (keyboard->focus != view)
         continue;

      if (view->client->input[WLC_KEYBOARD] == resource)
         wlc_keyboard_focus(keyboard, 0, NULL);
      view->client->input[WLC_KEYBOARD] = NULL;
      break;
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

   if (keyboard->clients) {
      struct wlc_client *client;
      wl_list_for_each(client, keyboard->clients, link)
         client->input[WLC_KEYBOARD] = NULL;
   }

   if (keyboard->state)
      xkb_state_unref(keyboard->state);

   free(keyboard);
}

struct wlc_keyboard*
wlc_keyboard_new(struct wlc_keymap *keymap, struct wl_list *clients, struct wl_list *views)
{
   assert(keymap && views);

   struct wlc_keyboard *keyboard;
   if (!(keyboard = calloc(1, sizeof(struct wlc_keyboard))))
      goto fail;

   if (!wlc_keyboard_set_keymap(keyboard, keymap))
      goto fail;

   keyboard->clients = clients;
   keyboard->views = views;
   return keyboard;

fail:
   if (keyboard)
      wlc_keyboard_free(keyboard);
   return NULL;
}
