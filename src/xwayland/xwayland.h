#ifndef _WLC_XWAYLAND_H_
#define _WLC_XWAYLAND_H_

#include <stdbool.h>

struct wl_client* wlc_xwayland_get_client(void);
int wlc_xwayland_get_fd(void);
bool wlc_xwayland_init(void);
void wlc_xwayland_terminate(void);

#endif /* _WLC_XWAYLAND_H_ */
