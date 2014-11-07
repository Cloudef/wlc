#ifndef _WLC_POINTER_H_
#define _WLC_POINTER_H_

#include <stdint.h>
#include <stdbool.h>
#include <wayland-util.h>

#include "types/geometry.h"

enum grab_action {
   WLC_GRAB_ACTION_NONE,
   WLC_GRAB_ACTION_MOVE,
   WLC_GRAB_ACTION_RESIZE
};

enum wl_pointer_button_state;
enum wl_pointer_axis;

struct wl_list;
struct wl_resource;
struct wlc_view;
struct wlc_client;
struct wlc_surface;
struct wlc_render;

struct wlc_pointer {
   struct wlc_compositor *compositor;
   struct wlc_surface *surface;
   struct wlc_view *focus;

   struct wlc_origin tip;
   struct wlc_origin pos;
   struct wlc_origin grab;

   uint32_t action_edges;
   enum grab_action action;
   bool grabbing;
};

void wlc_pointer_focus(struct wlc_pointer *pointer, struct wlc_view *view, struct wlc_origin *out_pos);
void wlc_pointer_button(struct wlc_pointer *pointer, uint32_t time, uint32_t button, enum wl_pointer_button_state state);
void wlc_pointer_scroll(struct wlc_pointer *pointer, uint32_t time, enum wl_pointer_axis axis, double amount);
void wlc_pointer_motion(struct wlc_pointer *pointer, uint32_t time, const struct wlc_origin *pos);
void wlc_pointer_remove_client_for_resource(struct wlc_pointer *pointer, struct wl_resource *resource);
void wlc_pointer_set_surface(struct wlc_pointer *pointer, struct wlc_surface *surface, const struct wlc_origin *tip);
void wlc_pointer_paint(struct wlc_pointer *pointer, struct wlc_render *render);
void wlc_pointer_free(struct wlc_pointer *pointer);
struct wlc_pointer* wlc_pointer_new(struct wlc_compositor *compositor);

#endif /* _WLC_POINTER_H_ */
