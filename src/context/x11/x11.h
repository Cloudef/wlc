#ifndef _WLC_X11_H_
#define _WLC_X11_H_

#include <X11/Xlib.h>
#include <stdbool.h>

struct wlc_seat;

Display* wlc_x11_display(void);
Window wlc_x11_window(void);
int wlc_x11_event_fd(void);
int wlc_x11_poll_events(struct wlc_seat *seat);
void wlc_x11_terminate(void);
bool wlc_x11_init(void);

#endif /* _WLC_X11_H_ */
