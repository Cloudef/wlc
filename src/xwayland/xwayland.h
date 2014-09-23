#ifndef _WLC_XWAYLAND_H_
#define _WLC_XWAYLAND_H_

#include <stdbool.h>

struct wlc_compositor;

bool wlc_xwayland_init(struct wlc_compositor *compositor);
void wlc_xwayland_deinit(void);

#endif /* _WLC_XWAYLAND_H_ */
