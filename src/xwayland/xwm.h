#ifndef _WLC_XWM_H_
#define _WLC_XWM_H_

#include <stdbool.h>
#include <stdint.h>
#include <chck/lut/lut.h>

enum wlc_view_state_bit;
enum wlc_surface_format;

struct wlc_x11_window {
   uint32_t id; // xcb_window_t
   uint32_t surface_id;
   bool override_redirect;
   bool has_delete_window;
   bool has_alpha;
};

struct wlc_xwm {
   struct wl_event_source *event_source;
   struct chck_hash_table paired, unpaired;

   struct {
      struct wl_listener surface;
   } listener;
};

enum wlc_surface_format wlc_x11_window_get_surface_format(struct wlc_x11_window *win);
void wlc_x11_window_position(struct wlc_x11_window *win, int32_t x, int32_t y);
void wlc_x11_window_resize(struct wlc_x11_window *win, uint32_t width, uint32_t height);
void wlc_x11_window_set_state(struct wlc_x11_window *win, enum wlc_view_state_bit state, bool toggle);
void wlc_x11_window_set_active(struct wlc_x11_window *win, bool active);
void wlc_x11_window_close(struct wlc_x11_window *win);

bool wlc_xwm(struct wlc_xwm *xwm);
void wlc_xwm_release(struct wlc_xwm *xwm);

#endif /* _WLC_XWM_H_ */
