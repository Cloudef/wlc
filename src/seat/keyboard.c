#include "keyboard.h"
#include "keymap.h"

#include <stdlib.h>
#include <assert.h>

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

   if (keyboard->state)
      xkb_state_unref(keyboard->state);

   free(keyboard);
}

struct wlc_keyboard*
wlc_keyboard_new(struct wlc_keymap *keymap)
{
   struct wlc_keyboard *keyboard;

   if (!(keyboard = calloc(1, sizeof(struct wlc_keyboard))))
      goto fail;

   if (!wlc_keyboard_set_keymap(keyboard, keymap))
      goto fail;

   wl_list_init(&keyboard->resource_list);
   return keyboard;

fail:
   if (keyboard)
      wlc_keyboard_free(keyboard);
   return NULL;
}
