#include <stdlib.h>
#include <assert.h>
#include <wayland-server.h>
#include "internal.h"
#include "macros.h"
#include "keyboard.h"
#include "keymap.h"
#include "compositor/view.h"

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

      wlc_dlog(WLC_DBG_KEYBOARD, "remove key: %u", *k);
      chck_iter_pool_remove(keys, --_I);
   }

   if (state == WL_KEYBOARD_KEY_STATE_PRESSED && !chck_iter_pool_push_back(keys, &key))
      return false;

   if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
      wlc_dlog(WLC_DBG_KEYBOARD, "add key: %u", key);

   return true;
}

static void
send_release_for_keys(wlc_resource resource, struct chck_iter_pool *keys)
{
   assert(keys);

   struct wl_resource *focus;
   if (!(focus = wl_resource_from_wlc_resource(resource, "keyboard")))
      return;

   wlc_dlog(WLC_DBG_KEYBOARD, "release keys for resource %zu", resource);

   uint32_t *k;
   uint32_t time = wlc_get_time(NULL);
   chck_iter_pool_for_each(keys, k) {
      uint32_t serial = wl_display_next_serial(wlc_display());
      wl_keyboard_send_key(focus, serial, time, *k, WL_KEYBOARD_KEY_STATE_RELEASED);
   }
}

static int
cb_repeat(void *data)
{
   struct wlc_keyboard *keyboard;
   except((keyboard = data));

   struct chck_iter_pool moved = keyboard->keys;
   keyboard->keys.items.buffer = NULL;
   chck_iter_pool_flush(&keyboard->keys);

   wl_event_source_timer_update(keyboard->timer.repeat, 0);
   keyboard->state.focused = keyboard->state.repeat = false;

   // If we cause yet another repeat, we use repeat rate instead of delay.
   keyboard->state.repeating = true;

   if (keyboard->keymap) {
      // Repeat the repeating keys only

      uint32_t *k;
      chck_iter_pool_for_each(&moved, k) {
         if (xkb_keymap_key_repeats(keyboard->keymap->keymap, *k + 8)) {
            xkb_state_update_key(keyboard->state.xkb, *k + 8, XKB_KEY_UP);
         } else {
            update_keys(&keyboard->keys, *k, WL_KEYBOARD_KEY_STATE_PRESSED);
         }
      }

      chck_iter_pool_for_each(&moved, k) {
         if (!xkb_keymap_key_repeats(keyboard->keymap->keymap, *k + 8))
            continue;

         struct wlc_input_event ev;
         ev.type = WLC_INPUT_EVENT_KEY;
         ev.time = wlc_get_time(NULL);
         ev.key.code = *k;
         ev.key.state = WL_KEYBOARD_KEY_STATE_PRESSED;
         wl_signal_emit(&wlc_system_signals()->input, &ev);
      }
   }

   chck_iter_pool_release(&moved);
   wlc_dlog(WLC_DBG_KEYBOARD, "wlc key repeat");
   return 1;
}

static void
begin_repeat(struct wlc_keyboard *keyboard, bool focused)
{
   keyboard->state.repeat = true;
   keyboard->state.focused = focused;
   const uint32_t delay = (keyboard->state.repeating ? keyboard->repeat.rate : keyboard->repeat.delay);
   wl_event_source_timer_update(keyboard->timer.repeat, delay);
   wlc_dlog(WLC_DBG_KEYBOARD, "begin wlc key repeat (%d : %d)", focused, keyboard->state.repeating);
}

static void
reset_repeat(struct wlc_keyboard *keyboard)
{
   if (!keyboard->state.repeat)
      return;

   wl_event_source_timer_update(keyboard->timer.repeat, 0);
   keyboard->state.repeating = keyboard->state.focused = keyboard->state.repeat = false;
   wlc_dlog(WLC_DBG_KEYBOARD, "canceled wlc key repeat");
}

void
wlc_keyboard_update_modifiers(struct wlc_keyboard *keyboard)
{
   assert(keyboard);

   uint32_t depressed = xkb_state_serialize_mods(keyboard->state.xkb, XKB_STATE_DEPRESSED);
   uint32_t latched = xkb_state_serialize_mods(keyboard->state.xkb, XKB_STATE_LATCHED);
   uint32_t locked = xkb_state_serialize_mods(keyboard->state.xkb, XKB_STATE_LOCKED);
   uint32_t group = xkb_state_serialize_layout(keyboard->state.xkb, XKB_STATE_LAYOUT_EFFECTIVE);

   if (depressed == keyboard->mods.depressed &&
       latched == keyboard->mods.latched &&
       locked == keyboard->mods.locked &&
       group == keyboard->mods.group)
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

   if (keyboard->keymap) {
      keyboard->modifiers.mods = wlc_keymap_get_mod_mask(keyboard->keymap, depressed | latched);
      keyboard->modifiers.leds = wlc_keymap_get_led_mask(keyboard->keymap, keyboard->state.xkb);
   }

   wlc_dlog(WLC_DBG_KEYBOARD, "updated modifiers");
}

bool
wlc_keyboard_request_key(struct wlc_keyboard *keyboard, uint32_t time, const struct wlc_modifiers *mods, uint32_t key, enum wl_keyboard_key_state state)
{
   uint32_t sym = xkb_state_key_get_one_sym(keyboard->state.xkb, key + 8);

   if (WLC_INTERFACE_EMIT_EXCEPT(keyboard.key, false, keyboard->focused.view, time, mods, key, sym, (enum wlc_key_state)state)) {
      if (state == WL_KEYBOARD_KEY_STATE_PRESSED && keyboard->keymap && xkb_keymap_key_repeats(keyboard->keymap->keymap, key + 8))
         begin_repeat(keyboard, false);
      return false;
   }

   return true;
}

bool
wlc_keyboard_update(struct wlc_keyboard *keyboard, uint32_t key, enum wl_keyboard_key_state state)
{
   assert(keyboard);

   xkb_state_update_key(keyboard->state.xkb, key + 8, (state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP));
   const bool ret = update_keys(&keyboard->keys, key, state);

   if (ret)
      reset_repeat(keyboard);

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

   if (!keyboard->state.repeating)
      reset_repeat(keyboard);

   send_release_for_keys(keyboard->focused.resource, &keyboard->keys);

   {
      struct wlc_view *v;
      struct wl_resource *surface, *focus;
      if ((v = convert_from_wlc_handle(keyboard->focused.view, "view")) &&
          (surface = wl_resource_from_wlc_resource(v->surface, "surface")) &&
          (focus = wl_resource_from_wlc_resource(keyboard->focused.resource, "keyboard"))) {
         if ((!view || !view->x11.id) && v->x11.id)
            wlc_x11_window_set_active(&v->x11, false);

         uint32_t serial = wl_display_next_serial(wlc_display());
         wl_keyboard_send_leave(focus, serial, surface);

         if (v->xdg_popup)
            wlc_view_close_ptr(v);
      }
   }

   if (keyboard->focused.view)
      WLC_INTERFACE_EMIT(view.focus, keyboard->focused.view, false);

   {
      struct wl_client *client;
      struct wl_resource *surface, *focus = NULL;
      if (view && (surface = wl_resource_from_wlc_resource(view->surface, "surface")) &&
          (client = wl_resource_get_client(surface)) &&
          (focus = wl_resource_for_client(&keyboard->resources, client))) {

         {
            uint32_t serial = wl_display_next_serial(wlc_display());
            wl_keyboard_send_modifiers(focus, serial, keyboard->mods.depressed, keyboard->mods.latched, keyboard->mods.locked, keyboard->mods.group);
         }

         {
            struct wl_array keys;
            wl_array_init(&keys);

            uint32_t repeating = 0;
            if (keyboard->keymap) {
               // Send the non-repeating keys (usually modifiers) on focus.

               uint32_t *k;
               chck_iter_pool_for_each(&keyboard->keys, k) {
                  if (xkb_keymap_key_repeats(keyboard->keymap->keymap, *k + 8)) {
                     ++repeating;
                     continue;
                  }

                  uint32_t *tmp;
                  if ((tmp = wl_array_add(&keys, sizeof(uint32_t))))
                     *tmp = *k;

                  wlc_dlog(WLC_DBG_KEYBOARD, "focus key: %u", *k);
               }
            }

            uint32_t serial = wl_display_next_serial(wlc_display());
            wl_keyboard_send_enter(focus, serial, surface, &keys);
            wl_array_release(&keys);

            if (repeating > 0) {
               // Send the repeating keys later to the view.
               // This is because, we don't want to leak input when for example you close something and the focus switches.
               // It also avoids input spamming.
               //
               // This send is canceled if anything is pressed before timeout.
               wlc_dlog(WLC_DBG_KEYBOARD, "seding repeating keys to focus (%u)", repeating);
               begin_repeat(keyboard, true);
            }
         }
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

   keyboard->keymap = keymap;
   return true;
}

void
wlc_keyboard_release(struct wlc_keyboard *keyboard)
{
   if (!keyboard)
      return;

   if (keyboard->state.xkb)
      xkb_state_unref(keyboard->state.xkb);

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

   if (!(keyboard->timer.repeat = wl_event_loop_add_timer(wlc_event_loop(), cb_repeat, keyboard)))
      goto fail;

   const char *delay = getenv("WLC_REPEAT_DELAY"), *rate = getenv("WLC_REPEAT_RATE");
   keyboard->repeat.delay = (chck_cstr_is_empty(delay) ? 660 : strtol(delay, NULL, 10));
   keyboard->repeat.rate = (chck_cstr_is_empty(rate) ? 25 : strtol(rate, NULL, 10));
   return true;

fail:
   wlc_keyboard_release(keyboard);
   return false;
}
