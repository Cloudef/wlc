#ifndef _WLC_GLX_H_
#define _WLC_GLX_H_

#include <wayland-server.h>
#include <stdbool.h>

void wlc_glx_swap_buffers(void);
bool wlc_glx_has_extension(const char *extension);
void wlc_glx_terminate(void);
bool wlc_glx_init(struct wl_display *display);

#endif /* _WLC_GLX_H_ */
