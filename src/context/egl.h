#ifndef _WLC_EGL_H_
#define _WLC_EGL_H_

#include <stdbool.h>

struct wl_display;
struct wlc_context;

bool wlc_egl_init(struct wl_display *display, struct wlc_context *out_context);

#endif /* _WLC_EGL_H_ */
