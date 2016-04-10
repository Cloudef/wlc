#ifndef _WLC_XWAYLAND_H_
#define _WLC_XWAYLAND_H_

#include <stdbool.h>

#ifdef ENABLE_XWAYLAND

struct wl_client* wlc_xwayland_get_client(void);
int wlc_xwayland_get_fd(void);
bool wlc_xwayland_init(void);
void wlc_xwayland_terminate(void);

#else

static inline bool
wlc_xwayland_init(void)
{
   return false;
}

static inline void
wlc_xwayland_terminate(void)
{
}

#endif

#endif /* _WLC_XWAYLAND_H_ */
