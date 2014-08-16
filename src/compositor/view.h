#ifndef _WLC_VIEW_H_
#define _WLC_VIEW_H_

#include <wayland-util.h>
#include <stdbool.h>

enum wlc_view_bit {
   WLC_BIT_MAXIMIZED = 1<<0,
   WLC_BIT_FULLSCREEN = 1<<1,
   WLC_BIT_RESIZING = 1<<2,
   WLC_BIT_ACTIVATED = 1<<3,
};

struct wl_resource;
struct wlc_client;
struct wlc_surface;
struct wlc_shell_surface;
struct wlc_xdg_surface;

struct wlc_view {
   struct wlc_client *client;
   struct wlc_surface *surface;
   struct wlc_shell_surface *shell_surface;
   struct wlc_xdg_surface *xdg_surface;
   struct wl_list link, user_link;
   struct wl_array stored_state;
   uint32_t state;
   int32_t x, y;
};

struct wlc_view* wlc_view_for_surface_in_list(struct wlc_surface *surface, struct wl_list *list);
void wlc_view_set_xdg_surface(struct wlc_view *view, struct wlc_xdg_surface *xdg_surface);
void wlc_view_set_shell_surface(struct wlc_view *view, struct wlc_shell_surface *shell_surface);
void wlc_view_free(struct wlc_view *view);
struct wlc_view* wlc_view_new(struct wlc_client *client, struct wlc_surface *compositor);
void wlc_view_position(struct wlc_view *view, int32_t x, int32_t y);
void wlc_view_resize(struct wlc_view *view, uint32_t width, uint32_t height);

#endif /* _WLC_VIEW_H_ */
