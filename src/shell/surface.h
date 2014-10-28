#ifndef _WLC_SHELL_SURFACE_H_
#define _WLC_SHELL_SURFACE_H_

#include <wayland-server-protocol.h>

#include "types/string.h"

struct wl_resource;
struct wlc_view;

// Inherted by wlc_view
struct wlc_shell_surface {
   struct wl_resource *resource;
   struct wlc_string title;
   struct wlc_string _class;
   enum wl_shell_surface_fullscreen_method fullscreen_mode;
};

void wlc_shell_surface_implement(struct wlc_shell_surface *shell_surface, struct wlc_view *view, struct wl_resource *resource);
void wlc_shell_surface_set_title(struct wlc_shell_surface *shell_surface, const char *title);
void wlc_shell_surface_set_class(struct wlc_shell_surface *shell_surface, const char *class_);
void wlc_shell_surface_release(struct wlc_shell_surface *shell_surface);

#endif /* _WLC_SHELL_SURFACE_H_ */
