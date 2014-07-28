#ifndef _WLC_SHELL_SURFACE_H_
#define _WLC_SHELL_SURFACE_H_

#include <wayland-server.h>
#include "types/string.h"

struct wlc_shell_surface {
   struct wl_resource *resource;
   struct wlc_surface *surface;
   struct wlc_shell_surface *parent;
   void *output;
   struct wlc_string title;
   struct wlc_string class;

   struct wlc_geometry {
      int32_t x, y;
      int32_t w, h;
   } geometry;

   bool fullscreen;
   bool maximized;
};

void wlc_shell_surface_implement(struct wlc_shell_surface *shell_surface, struct wl_resource *resource);
void wlc_shell_surface_set_parent(struct wlc_shell_surface *shell_surface, struct wlc_shell_surface *parent);
void wlc_shell_surface_set_output(struct wlc_shell_surface *shell_surface, void *output);
void wlc_shell_surface_set_title(struct wlc_shell_surface *shell_surface, const char *title);
void wlc_shell_surface_set_class(struct wlc_shell_surface *shell_surface, const char *class_);
void wlc_shell_surface_set_geometry(struct wlc_shell_surface *shell_surface, int32_t x, int32_t y, int32_t width, int32_t height);
void wlc_shell_surface_set_fullscreen(struct wlc_shell_surface *shell_surface, bool fullscreen);
void wlc_shell_surface_set_maximized(struct wlc_shell_surface *shell_surface, bool maximized);
void wlc_shell_surface_free(struct wlc_shell_surface *shell_surface);
struct wlc_shell_surface* wlc_shell_surface_new(struct wlc_surface *surface);

#endif /* _WLC_SHELL_SURFACE_H_ */
