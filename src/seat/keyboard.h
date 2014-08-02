#ifndef _WLC_KEYBOARD_H_
#define _WLC_KEYBOARD_H_

#include <stdbool.h>
#include <wayland-util.h>

struct xkb_state;
struct wlc_keymap;

struct wlc_keyboard {
   struct wl_resource *focus;
   struct wl_list resource_list;
   struct xkb_state *state;

   struct {
      uint32_t depressed;
      uint32_t latched;
      uint32_t locked;
      uint32_t group;
   } mods;
};

bool wlc_keyboard_set_keymap(struct wlc_keyboard *keyboard, struct wlc_keymap *keymap);
void wlc_keyboard_free(struct wlc_keyboard *keyboard);
struct wlc_keyboard* wlc_keyboard_new(struct wlc_keymap *keymap);

#endif /* _WLC_KEYBOARD_H_ */
