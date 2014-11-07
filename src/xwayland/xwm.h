#ifndef _WLC_XWM_H_
#define _WLC_XWM_H_

#include <stdbool.h>
#include <stdint.h>

struct wl_client;
struct wlc_compositor;
struct wlc_surface;
struct wlc_x11_window;
struct wlc_xwm;

enum wlc_surface_format wlc_x11_window_get_surface_format(struct wlc_x11_window *win);
void wlc_x11_window_position(struct wlc_x11_window *win, const int32_t x, const int32_t y);
void wlc_x11_window_resize(struct wlc_x11_window *win, const uint32_t width, const uint32_t height);
void wlc_x11_window_set_state(struct wlc_x11_window *win, enum wlc_view_state_bit state, bool toggle);
void wlc_x11_window_set_active(struct wlc_x11_window *win, bool active);
void wlc_x11_window_close(struct wlc_x11_window *win);
void wlc_x11_window_free(struct wlc_x11_window *win);

struct wlc_xwm* wlc_xwm_new(struct wlc_compositor *compositor);
void wlc_xwm_free(struct wlc_xwm *xwm);

#endif /* _WLC_XWM_H_ */
