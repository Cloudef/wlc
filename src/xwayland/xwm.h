#ifndef _WLC_XWM_H_
#define _WLC_XWM_H_

#include <stdbool.h>

struct wlc_compositor;
struct wlc_surface;

bool wlc_xwm_init(struct wlc_compositor *compositor, const int fd);
void wlc_xwm_deinit(void);

#endif /* _WLC_XWM_H_ */
