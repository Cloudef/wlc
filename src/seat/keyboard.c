#include "keyboard.h"

#include <stdlib.h>
#include <assert.h>

void
wlc_keyboard_free(struct wlc_keyboard *keyboard)
{
   assert(keyboard);
   free(keyboard);
}

struct wlc_keyboard*
wlc_keyboard_new(void)
{
   struct wlc_keyboard *keyboard;

   if (!(keyboard = calloc(1, sizeof(struct wlc_keyboard))))
      return NULL;

   wl_list_init(&keyboard->resource_list);
   return keyboard;
}
