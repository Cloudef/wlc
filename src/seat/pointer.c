#include "pointer.h"

#include "compositor/view.h"
#include "compositor/surface.h"

#include <stdlib.h>
#include <assert.h>

#include <wayland-server.h>

void
wlc_pointer_focus(struct wlc_pointer *pointer, uint32_t serial, struct wlc_view *view, int32_t x,  int32_t y)
{
   assert(pointer);

   if (pointer->focus == view)
      return;

   if (!view) {
      if (pointer->focus) {
         pointer->focus = NULL;
         pointer->grabbing = false;
         pointer->action = WLC_GRAB_ACTION_NONE;
         pointer->action_edges = 0;
      }
      return;
   }

   if (pointer->focus && pointer->focus->input[WLC_POINTER]) {
      struct wlc_view *focus = pointer->focus;
      wl_pointer_send_leave(focus->input[WLC_POINTER], serial, focus->surface->resource);
   }

   if (view->input[WLC_POINTER]) {
      wl_pointer_send_enter(view->input[WLC_POINTER], serial, view->surface->resource, wl_fixed_from_int(x), wl_fixed_from_int(y));
      pointer->focus = view;
   } else {
      pointer->focus = NULL;
   }

   pointer->grabbing = false;
   pointer->action = WLC_GRAB_ACTION_NONE;
   pointer->action_edges = 0;
}

void
wlc_pointer_button(struct wlc_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, enum wl_pointer_button_state state)
{
   assert(pointer);

   if (!pointer->focus)
      return;

   if (state == WL_POINTER_BUTTON_STATE_PRESSED && !pointer->grabbing) {
      pointer->grabbing = true;
      pointer->gx = pointer->x;
      pointer->gy = pointer->y;
   } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
      pointer->grabbing = false;
      pointer->action = WLC_GRAB_ACTION_NONE;
      pointer->action_edges = 0;
   }

   wl_pointer_send_button(pointer->focus->input[WLC_POINTER], serial, time, button, state);
}

void
wlc_pointer_motion(struct wlc_pointer *pointer, uint32_t serial, uint32_t time, int32_t x, int32_t y)
{
   assert(pointer);

   pointer->x = wl_fixed_from_int(x);
   pointer->y = wl_fixed_from_int(y);

   struct wlc_view *focused = NULL;
   if (pointer->focus && pointer->grabbing) {
      focused = pointer->focus;
   } else {
      struct wlc_view *view;
      wl_list_for_each(view, pointer->views, link) {
         if (x >= view->x && x <= view->x + view->surface->width &&
             y >= view->y && y <= view->y + view->surface->height) {
            focused = view;
            break;
         }
      }
   }

   if (!focused) {
      wlc_pointer_focus(pointer, 0, NULL, 0, 0);
      return;
   }

   int32_t dx = x - focused->x;
   int32_t dy = y - focused->y;
   wlc_pointer_focus(pointer, serial, focused, dx, dy);

   if (!focused->input[WLC_POINTER])
      return;

   wl_pointer_send_motion(focused->input[WLC_POINTER], time, wl_fixed_from_int(dx), wl_fixed_from_int(dy));

   if (pointer->grabbing) {
      int32_t dx = x - wl_fixed_to_int(pointer->gx);
      int32_t dy = y - wl_fixed_to_int(pointer->gy);

      if (pointer->action == WLC_GRAB_ACTION_MOVE) {
         focused->x += dx;
         focused->y += dy;
      } else if (pointer->action == WLC_GRAB_ACTION_RESIZE) {
#if 0
         if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_LEFT) {
            focused->w -= dx;
            focused->x += dx;
         } else if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_RIGHT) {
            focused->w += dx;
         }

         if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_TOP) {
            focused->h -= dy;
            focused->y += dy;
         } else if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_BOTTOM) {
            focused->h += dy;
         }
#endif
      }

      pointer->gx = pointer->x;
      pointer->gy = pointer->y;
   }
}

void
wlc_pointer_remove_view_for_resource(struct wlc_pointer *pointer, struct wl_resource *resource)
{
   assert(pointer && resource);

   struct wlc_view *view;
   if ((view = wlc_view_for_input_resource_in_list(resource, WLC_POINTER, pointer->views))) {
      if (pointer->focus == view)
         wlc_pointer_focus(pointer, 0, NULL, 0, 0);

      view->input[WLC_POINTER] = NULL;
   }
}

void
wlc_pointer_free(struct wlc_pointer *pointer)
{
   assert(pointer);

   struct wlc_view *view;
   wl_list_for_each(view, pointer->views, link)
      view->input[WLC_POINTER] = NULL;

   free(pointer);
}

struct wlc_pointer*
wlc_pointer_new(struct wl_list *views)
{
   assert(views);

   struct wlc_pointer *pointer;
   if (!(pointer = calloc(1, sizeof(struct wlc_pointer))))
      return NULL;

   pointer->views = views;
   return pointer;
}
