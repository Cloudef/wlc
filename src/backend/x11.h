#ifndef _WLC_X11_H_
#define _WLC_X11_H_

#include <stdbool.h>

struct wlc_backend;
struct wlc_compositor;

bool wlc_x11_init(struct wlc_backend *out_backend, struct wlc_compositor *compositor);

#endif /* _WLC_X11_H_ */
