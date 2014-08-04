#ifndef _WLC_VIEW_H_
#define _WLC_VIEW_H_

#include <wayland-util.h>
#include <stdbool.h>

enum wlc_input_type {
   WLC_KEYBOARD,
   WLC_POINTER,
   WLC_TOUCH,
   WLC_INPUT_TYPE_LAST
};

struct wl_client;
struct wl_resource;
struct wlc_surface;
struct wlc_shell_surface;
struct wlc_xdg_surface;

struct wlc_view {
   struct wlc_surface *surface;
   struct wlc_shell_surface *shell_surface;
   struct wlc_xdg_surface *xdg_surface;
   struct wl_resource *input[WLC_INPUT_TYPE_LAST];
   struct wl_array state;
   struct wl_list link, user_link;
   int32_t x, y;
};

struct wl_client* wlc_view_get_client(struct wlc_view *view);
struct wlc_view* wlc_view_for_input_resource_in_list(struct wl_resource *resource, enum wlc_input_type type, struct wl_list *list);
struct wlc_view* wlc_view_for_surface_in_list(struct wlc_surface *surface, struct wl_list *list);
struct wlc_view* wlc_view_for_client_in_list(struct wl_client *client, struct wl_list *list);
void wlc_view_set_xdg_surface(struct wlc_view *view, struct wlc_xdg_surface *xdg_surface);
void wlc_view_set_shell_surface(struct wlc_view *view, struct wlc_shell_surface *shell_surface);
void wlc_view_free(struct wlc_view *view);
struct wlc_view* wlc_view_new(struct wlc_surface *compositor);
void wlc_view_set_active(struct wlc_view *view, bool active);

#endif /* _WLC_VIEW_H_ */
