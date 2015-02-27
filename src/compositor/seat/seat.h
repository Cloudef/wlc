#ifndef _WLC_SEAT_H_
#define _WLC_SEAT_H_

#include <stdint.h>
#include <wlc/wlc.h>
#include <wayland-server.h>
#include "data.h"
#include "keymap.h"
#include "keyboard.h"
#include "pointer.h"
#include "touch.h"

struct wl_global;

struct wlc_seat {
   struct wlc_data_device_manager manager;
   struct wlc_keymap keymap;
   struct wlc_keyboard keyboard;
   struct wlc_pointer pointer;
   struct wlc_touch touch;

   // For interface calls
   struct wlc_modifiers modifiers;

   struct {
      struct wl_global *seat;
   } wl;

   struct {
      struct wl_listener input;
      struct wl_listener focus;
      struct wl_listener surface;
   } listener;
};

void wlc_seat_release(struct wlc_seat *seat);
bool wlc_seat(struct wlc_seat *seat);

#endif /* _WLC_SEAT_H_ */
