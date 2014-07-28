#ifndef _WLC_X11_H_
#define _WLC_X11_H_

#include <X11/Xlib.h>
#include <stdbool.h>

Display* wlc_x11_display(void);
int wlc_x11_screen(void);
Window wlc_x11_window(void);
void wlc_x11_terminate(void);
bool wlc_x11_init(void);

#endif /* _WLC_X11_H_ */
