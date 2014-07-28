#ifndef _WLC_EGL_H_
#define _WLC_EGL_H_

#include <wayland-egl.h>
#include <stdbool.h>

void wlc_egl_swap_buffers(void);
bool wlc_egl_has_extension(const char *extension);
void wlc_egl_terminate(void);
bool wlc_egl_init(struct wl_display *display);

#endif /* _WLC_EGL_H_ */
