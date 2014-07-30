#ifndef _WLC_SEAT_H_
#define _WLC_SEAT_H_

#include <stdint.h>
#include <wayland-server-protocol.h>

struct wl_global;
struct wlc_pointer;
struct wlc_compositor;

struct wlc_seat {
   struct wl_global *global;
   struct wlc_pointer *pointer;
   struct wlc_compositor *compositor;

   struct {
      void (*pointer_motion)(struct wlc_seat *seat, int32_t x, int32_t y);
      void (*pointer_button)(struct wlc_seat *seat, uint32_t button, enum wl_pointer_button_state state);
   } notify;
};

void wlc_seat_free(struct wlc_seat *seat);
struct wlc_seat* wlc_seat_new(struct wlc_compositor *compositor);

#endif /* _WLC_SEAT_H_ */
