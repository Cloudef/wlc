#ifndef _WLC_XWM_H_
#define _WLC_XWM_H_

#include <stdbool.h>
#include <stdint.h>

struct wl_client;
struct wlc_compositor;
struct wlc_surface;
struct wlc_x11_window;

enum wlc_surface_format wlc_x11_window_get_surface_format(struct wlc_x11_window *win);
void wlc_x11_window_position(struct wlc_x11_window *win, const int32_t x, const int32_t y);
void wlc_x11_window_resize(struct wlc_x11_window *win, const uint32_t width, const uint32_t height);
void wlc_x11_window_set_active(struct wlc_x11_window *win, bool active);
void wlc_x11_window_close(struct wlc_x11_window *win);
void wlc_x11_window_free(struct wlc_x11_window *win);

void wlc_xwm_surface_notify(struct wlc_compositor *compositor);
bool wlc_xwm_init(struct wlc_compositor *compositor, struct wl_client *client, const int fd);
void wlc_xwm_deinit(void);

#endif /* _WLC_XWM_H_ */
