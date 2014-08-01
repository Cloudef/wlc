#ifndef _WLC_KEYBOARD_H_
#define _WLC_KEYBOARD_H_

#include <stdbool.h>
#include <wayland-util.h>

struct wlc_keyboard {
   struct wl_list resource_list;
   struct wl_resource *focus;
};

void wlc_keyboard_free(struct wlc_keyboard *keyboard);
struct wlc_keyboard* wlc_keyboard_new(void);

#endif /* _WLC_KEYBOARD_H_ */
