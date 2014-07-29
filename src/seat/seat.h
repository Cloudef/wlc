#ifndef _WLC_SEAT_H_
#define _WLC_SEAT_H_

struct wl_global;
struct wlc_compositor;

struct wlc_seat {
   struct wl_global *global;
   struct wlc_compositor *compositor;
};

void wlc_seat_free(struct wlc_seat *seat);
struct wlc_seat* wlc_seat_new(struct wlc_compositor *compositor);

#endif /* _WLC_SEAT_H_ */
