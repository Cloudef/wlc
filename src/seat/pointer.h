#ifndef _WLC_POINTER_H_
#define _WLC_POINTER_H_

#include <stdint.h>
#include <stdbool.h>
#include <wayland-util.h>

enum grab_action {
   WLC_GRAB_ACTION_NONE,
   WLC_GRAB_ACTION_MOVE,
   WLC_GRAB_ACTION_RESIZE
};

enum wl_pointer_button_state;

struct wl_list;
struct wl_resource;
struct wlc_view;
struct wlc_client;

struct wlc_pointer {
   struct wl_list *clients, *views;
   struct wlc_view *focus;

   wl_fixed_t x, y;
   wl_fixed_t gx, gy;
   uint32_t action_edges;
   enum grab_action action;
   bool grabbing;
};

void wlc_pointer_focus(struct wlc_pointer *pointer, uint32_t serial, struct wlc_view *view, int32_t x, int32_t y);
void wlc_pointer_button(struct wlc_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, enum wl_pointer_button_state state);
void wlc_pointer_motion(struct wlc_pointer *pointer, uint32_t serial, uint32_t time, int32_t x, int32_t y);
void wlc_pointer_remove_client_for_resource(struct wlc_pointer *pointer, struct wl_resource *resource);
void wlc_pointer_free(struct wlc_pointer *pointer);
struct wlc_pointer* wlc_pointer_new(struct wl_list *clients, struct wl_list *views);

#endif /* _WLC_POINTER_H_ */
