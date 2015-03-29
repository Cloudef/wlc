#ifndef _WLC_POINTER_H_
#define _WLC_POINTER_H_

#include <stdint.h>
#include <stdbool.h>
#include <wayland-util.h>
#include "resources/resources.h"

enum grab_action {
   WLC_GRAB_ACTION_NONE,
   WLC_GRAB_ACTION_MOVE,
   WLC_GRAB_ACTION_RESIZE
};

enum wl_pointer_button_state;
enum wl_pointer_axis;
enum wl_touch_type;
enum wlc_touch_type;

struct wlc_render;
struct wlc_surface;
struct wlc_view;

// We want to store internally for sub-pixel precision
// Events to wlc goes as wlc_origin though.
// May need to change that.
struct wlc_pointer_origin {
   double x, y;
};

struct wlc_pointer {
   struct wlc_source resources;
   struct wlc_pointer_origin pos;
   struct wlc_origin tip;

   wlc_resource surface;

   struct {
      struct chck_iter_pool resources;
      wlc_handle view;
   } focused;

   struct {
      struct wlc_origin grab;
      uint32_t action_edges;
      enum grab_action action;
      bool grabbing;
   } state;

   struct {
      struct wl_listener render;
   } listener;
};

const struct wl_pointer_interface wl_pointer_implementation;

void wlc_pointer_focus(struct wlc_pointer *pointer, struct wlc_view *view, struct wlc_pointer_origin *out_pos);
void wlc_pointer_button(struct wlc_pointer *pointer, uint32_t time, uint32_t button, enum wl_pointer_button_state state);
void wlc_pointer_scroll(struct wlc_pointer *pointer, uint32_t time, uint8_t axis_bits, double amount[2]);
void wlc_pointer_motion(struct wlc_pointer *pointer, uint32_t time, const struct wlc_pointer_origin *pos);
void wlc_pointer_set_surface(struct wlc_pointer *pointer, struct wlc_surface *surface, const struct wlc_origin *tip);
void wlc_pointer_release(struct wlc_pointer *pointer);
bool wlc_pointer(struct wlc_pointer *pointer);

#endif /* _WLC_POINTER_H_ */
