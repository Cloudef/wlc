#ifndef _WLC_COMPOSITOR_H_
#define _WLC_COMPOSTIOR_H_

#include <wayland-server.h>
#include "shell/shell.h"
#include "shell/xdg-shell.h"

struct wlc_compositor {
   struct wl_display *display;
   struct wl_event_loop *event_loop;
   struct wlc_shell *shell;
   struct wlc_xdg_shell *xdg_shell;
};

void wlc_compositor_run(struct wlc_compositor *compositor);
void wlc_compositor_free(struct wlc_compositor *compositor);
struct wlc_compositor* wlc_compositor_new(void);

#endif /* _WLC_COMPOSITOR_H_ */
