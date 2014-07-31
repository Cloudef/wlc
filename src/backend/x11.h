#ifndef _WLC_X11_H_
#define _WLC_X11_H_

#include <stdbool.h>

struct wlc_backend;

bool wlc_x11_init(struct wlc_backend *out_backend);

#endif /* _WLC_X11_H_ */
