#ifndef _WLC_XWM_H_
#define _WLC_XWM_H_

#include <stdbool.h>
#include <stdint.h>

struct wlc_compositor;
struct wlc_surface;
struct wlc_x11_window;

void wlc_x11_window_resize(struct wlc_x11_window *win, const uint32_t width, const uint32_t height);
void wlc_x11_window_free(struct wlc_x11_window *win);

bool wlc_xwm_init(struct wlc_compositor *compositor, const int fd);
void wlc_xwm_deinit(void);

#endif /* _WLC_XWM_H_ */
