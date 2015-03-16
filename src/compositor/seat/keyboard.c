#include <stdlib.h>
#include <assert.h>
#include <wayland-server.h>
#include "internal.h"
#include "macros.h"
#include "keyboard.h"
#include "keymap.h"
#include "compositor/view.h"

static void
update_modifiers(struct wlc_keyboard *keyboard)
{
   assert(keyboard);
   uint32_t depressed = xkb_state_serialize_mods(keyboard->state.xkb, XKB_STATE_DEPRESSED);
   uint32_t latched = xkb_state_serialize_mods(keyboard->state.xkb, XKB_STATE_LATCHED);
   uint32_t locked = xkb_state_serialize_mods(keyboard->state.xkb, XKB_STATE_LOCKED);
   uint32_t group = xkb_state_serialize_layout(keyboard->state.xkb, XKB_STATE_LAYOUT_EFFECTIVE);

   if (depressed == keyboard->mods.depressed &&
       latched   == keyboard->mods.latched   &&
       locked    == keyboard->mods.locked    &&
       group     == keyboard->mods.group)
      return;

   keyboard->mods.depressed = depressed;
   keyboard->mods.latched = latched;
   keyboard->mods.locked = locked;
   keyboard->mods.group = group;

   struct wlc_resource *r;
   chck_pool_for_each(&keyboard->resources.pool, r) {
      struct wl_resource *resource;
      if (!(resource = convert_to_wl_resource(r, "keyboard")))
         continue;

      uint32_t serial = wl_display_next_serial(wlc_display());
      wl_keyboard_send_modifiers(resource, serial, keyboard->mods.depressed, keyboard->mods.latched, keyboard->mods.locked, keyboard->mods.group);
   }
}

static bool
update_keys(struct chck_iter_pool *keys, uint32_t key, enum wl_keyboard_key_state state)
{
   assert(keys);

   uint32_t *k;
   chck_iter_pool_for_each(keys, k) {
      if (*k != key)
         continue;

      if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
         return false;

      chck_iter_pool_remove(keys, --_I);
   }

   if (state == WL_KEYBOARD_KEY_STATE_PRESSED && !chck_iter_pool_push_back(keys, &key))
      return false;

   return true;
}

static void
send_release_for_keys(wlc_resource resource, struct chck_iter_pool *keys)
{
   assert(keys);

   struct wl_resource *focus;
   if (!(focus = wl_resource_from_wlc_resource(resource, "keyboard")))
      return;

   uint32_t *k;
   uint32_t time = wlc_get_time(NULL);
   chck_iter_pool_for_each(keys, k) {
      uint32_t serial = wl_display_next_serial(wlc_display());
      wl_keyboard_send_key(focus, serial, time, *k, WL_KEYBOARD_KEY_STATE_RELEASED);
   }
}

static void
send_press_for_keys(wlc_resource resource, struct chck_iter_pool *keys)
{
   assert(keys);

   struct wl_resource *focus;
   if (!(focus = wl_resource_from_wlc_resource(resource, "keyboard")))
      return;

   uint32_t *k;
   uint32_t time = wlc_get_time(NULL);
   chck_iter_pool_for_each(keys, k) {
      uint32_t serial = wl_display_next_serial(wlc_display());
      wl_keyboard_send_key(focus, serial, time, *k, WL_KEYBOARD_KEY_STATE_PRESSED);
   }
}

static int
cb_send_keys(void *data)
{
   struct wlc_keyboard *keyboard;
   except((keyboard = data));
   send_press_for_keys(keyboard->focused.resource, &keyboard->keys);
   wl_event_source_timer_update(keyboard->timer.focus, 0);
   keyboard->state.locked = false;
   return 1;
}

static int
cb_repeat(void *data)
{
   struct wlc_keyboard *keyboard;
   except((keyboard = data));
   chck_iter_pool_release(&keyboard->keys);
   wl_event_source_timer_update(keyboard->timer.repeat, 0);
   keyboard->state.repeat = false;
   return 1;
}

bool
wlc_keyboard_request_key(struct wlc_keyboard *keyboard, uint32_t time, const struct wlc_modifiers *mods, uint32_t key, enum wl_keyboard_key_state state)
{
   uint32_t sym = xkb_state_key_get_one_sym(keyboard->state.xkb, key + 8);

   if (WLC_INTERFACE_EMIT_EXCEPT(keyboard.key, false, keyboard->focused.view, time, mods, key, sym, (enum wlc_key_state)state)) {
      wl_event_source_timer_update(keyboard->timer.repeat, 90);
      keyboard->state.repeat = true;
      return false;
   }

   return true;
}

bool
wlc_keyboard_update(struct wlc_keyboard *keyboard, uint32_t key, enum wl_keyboard_key_state state)
{
   assert(keyboard);
   xkb_state_update_key(keyboard->state.xkb, key + 8, (state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP));
   update_modifiers(keyboard);
   const bool ret = update_keys(&keyboard->keys, key, state);

   if (keyboard->state.repeat)
      cb_repeat(keyboard);

   if (keyboard->state.locked)
      cb_send_keys(keyboard);

   return ret;
}

void
wlc_keyboard_key(struct wlc_keyboard *keyboard, uint32_t time, uint32_t key, enum wl_keyboard_key_state state)
{
   assert(keyboard);
   struct wl_resource *focus;
   if (!(focus = wl_resource_from_wlc_resource(keyboard->focused.resource, "keyboard")))
      return;

   uint32_t serial = wl_display_next_serial(wlc_display());
   wl_keyboard_send_key(focus, serial, time, key, state);
}

void
wlc_keyboard_focus(struct wlc_keyboard *keyboard, struct wlc_view *view)
{
   assert(keyboard);

   if (keyboard->focused.view == convert_to_wlc_handle(view))
      return;

   wlc_dlog(WLC_DBG_FOCUS, "-> keyboard focus event %zu, %zu", keyboard->focused.view, convert_to_wlc_handle(view));
   send_release_for_keys(keyboard->focused.resource, &keyboard->keys);

   {
      struct wlc_view *v;
      struct wl_resource *surface, *focus;
      if ((v = convert_from_wlc_handle(keyboard->focused.view, "view")) &&
          (surface = wl_resource_from_wlc_resource(v->surface, "surface")) &&
          (focus = wl_resource_from_wlc_resource(keyboard->focused.resource, "keyboard"))) {
         uint32_t serial = wl_display_next_serial(wlc_display());
         if (!view->x11.id)
            wlc_x11_window_set_active(&v->x11, false);
         wl_keyboard_send_leave(focus, serial, surface);

         if (v->xdg_popup)
            wlc_view_close_ptr(v);
      }
   }

   if (keyboard->focused.view)
      WLC_INTERFACE_EMIT(view.focus, keyboard->focused.view, false);

   {
      if (view)
         wlc_x11_window_set_active(&view->x11, true);

      struct wl_client *client;
      struct wl_resource *surface, *focus = NULL;
      if (view && (surface = wl_resource_from_wlc_resource(view->surface, "surface")) &&
          (client = wl_resource_get_client(surface)) &&
          (focus = wl_resource_for_client(&keyboard->resources, client))) {
         uint32_t serial = wl_display_next_serial(wlc_display());
         struct wl_array keys;
         wl_array_init(&keys);
         wl_keyboard_send_enter(focus, serial, surface, &keys);

         // do not send keys immediately, maybe make this timer tied to repeat rate
         keyboard->state.locked = true;
         wl_event_source_timer_update(keyboard->timer.focus, 100);
      }

      keyboard->focused.view = convert_to_wlc_handle(view);
      keyboard->focused.resource = wlc_resource_from_wl_resource(focus);
   }

   if (keyboard->focused.view)
      WLC_INTERFACE_EMIT(view.focus, keyboard->focused.view, true);
}

bool
wlc_keyboard_set_keymap(struct wlc_keyboard *keyboard, struct wlc_keymap *keymap)
{
   assert(keyboard);

   if (!keymap && keyboard->state.xkb)
      xkb_state_unref(keyboard->state.xkb);

   if (keymap && (!(keyboard->state.xkb = xkb_state_new(keymap->keymap))))
      return false;

   return true;
}

void
wlc_keyboard_release(struct wlc_keyboard *keyboard)
{
   if (!keyboard)
      return;

   if (keyboard->state.xkb)
      xkb_state_unref(keyboard->state.xkb);

   if (keyboard->timer.focus)
      wl_event_source_remove(keyboard->timer.focus);

   if (keyboard->timer.repeat)
      wl_event_source_remove(keyboard->timer.repeat);

   chck_iter_pool_release(&keyboard->keys);
   wlc_source_release(&keyboard->resources);
   memset(keyboard, 0, sizeof(struct wlc_keyboard));
}

bool
wlc_keyboard(struct wlc_keyboard *keyboard, struct wlc_keymap *keymap)
{
   assert(keyboard && keymap);
   memset(keyboard, 0, sizeof(struct wlc_keyboard));

   if (!wlc_keyboard_set_keymap(keyboard, keymap))
      goto fail;

   if (!chck_iter_pool(&keyboard->keys, 32, 0, sizeof(uint32_t)))
      goto fail;

   if (!wlc_source(&keyboard->resources, "keyboard", NULL, NULL, 32, sizeof(struct wlc_resource)))
      goto fail;

   if (!(keyboard->timer.focus = wl_event_loop_add_timer(wlc_event_loop(), cb_send_keys, keyboard)) ||
       !(keyboard->timer.repeat = wl_event_loop_add_timer(wlc_event_loop(), cb_repeat, keyboard)))
      goto fail;

   return true;

fail:
   wlc_keyboard_release(keyboard);
   return false;
}
