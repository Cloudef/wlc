#ifndef _WLC_SEAT_H_
#define _WLC_SEAT_H_

#include "wlc.h"
#include <stdint.h>
#include <wayland-server.h>

struct wl_global;
struct wlc_view;
struct wlc_pointer;
struct wlc_keyboard;
struct wlc_compositor;
struct wlc_data_device;
struct wlc_origin;

// XXX: Merge with wlc_compositor?
struct wlc_seat {
   struct wl_global *global;
   struct wlc_keymap *keymap;
   struct wlc_pointer *pointer;
   struct wlc_keyboard *keyboard;
   struct wlc_compositor *compositor;
   struct wlc_data_device *device;

   // for interface calls
   struct wlc_modifiers modifiers;

   struct {
      struct wl_listener input;
   } listener;

   struct {
      // FIXME: make into signal
      void (*keyboard_focus)(struct wlc_seat *seat, struct wlc_view *view);
      void (*view_unfocus)(struct wlc_seat *seat, struct wlc_view *view);
   } notify;
};

void wlc_seat_free(struct wlc_seat *seat);
struct wlc_seat* wlc_seat_new(struct wlc_compositor *compositor);

#endif /* _WLC_SEAT_H_ */
