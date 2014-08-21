#include "pointer.h"
#include "client.h"

#include "compositor/view.h"
#include "compositor/surface.h"
#include "compositor/compositor.h"

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

   if (pointer->focus && pointer->focus->client->input[WLC_POINTER])
      wl_pointer_send_leave(pointer->focus->client->input[WLC_POINTER], serial, pointer->focus->surface->resource);

   if (view->client->input[WLC_POINTER]) {
      wl_pointer_send_enter(view->client->input[WLC_POINTER], serial, view->surface->resource, wl_fixed_from_int(x), wl_fixed_from_int(y));
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

   if (!pointer->focus || !pointer->focus->client->input[WLC_POINTER])
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

   wl_pointer_send_button(pointer->focus->client->input[WLC_POINTER], serial, time, button, state);
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
      wl_list_for_each_reverse(view, pointer->views, link) {
         if (x >= view->geometry.x && x <= view->geometry.x + view->geometry.w &&
             y >= view->geometry.y && y <= view->geometry.y + view->geometry.h) {
            focused = view;
            break;
         }
      }
   }

   if (!focused) {
      wlc_pointer_focus(pointer, 0, NULL, 0, 0);
      return;
   }

   struct wlc_geometry b;
   wlc_view_get_bounds(focused, &b);
   int32_t dx = x - b.x;
   int32_t dy = y - b.y;
   wlc_pointer_focus(pointer, serial, focused, dx, dy);

   if (!focused->client->input[WLC_POINTER])
      return;

   wl_pointer_send_motion(focused->client->input[WLC_POINTER], time, wl_fixed_from_int(dx), wl_fixed_from_int(dy));

   if (pointer->grabbing) {
      int32_t dx = x - wl_fixed_to_int(pointer->gx);
      int32_t dy = y - wl_fixed_to_int(pointer->gy);

      if (pointer->action == WLC_GRAB_ACTION_MOVE) {
         const int32_t x = focused->geometry.x + dx, y = focused->geometry.y + dy;
         if (focused->surface->compositor->interface.view.move) {
            focused->surface->compositor->interface.view.move(focused->surface->compositor, focused, x, y);
         } else {
            wlc_view_position(focused, x, y);
         }
      } else if (pointer->action == WLC_GRAB_ACTION_RESIZE) {
         int32_t w = focused->surface->width, h = focused->surface->height;

         if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_LEFT) {
            w -= dx;
            focused->geometry.x += dx;
         } else if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_RIGHT) {
            w += dx;
         }

         if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_TOP) {
            h -= dy;
            focused->geometry.y += dy;
         } else if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_BOTTOM) {
            h += dy;
         }

         if (focused->surface->compositor->interface.view.resize) {
            focused->surface->compositor->interface.view.resize(focused->surface->compositor, focused, w, h);
         } else {
             wlc_view_resize(focused, w, h);
         }
      }

      pointer->gx = pointer->x;
      pointer->gy = pointer->y;
   }
}

void
wlc_pointer_remove_client_for_resource(struct wlc_pointer *pointer, struct wl_resource *resource)
{
   assert(pointer && resource);

   struct wlc_view *view;
   wl_list_for_each(view, pointer->views, link) {
      if (pointer->focus != view)
         continue;

      if (view->client->input[WLC_KEYBOARD] == resource)
         wlc_pointer_focus(pointer, 0, NULL, 0, 0);
      view->client->input[WLC_POINTER] = NULL;
      break;
   }
}

void
wlc_pointer_free(struct wlc_pointer *pointer)
{
   assert(pointer);

   if (pointer->clients) {
      struct wlc_client *client;
      wl_list_for_each(client, pointer->clients, link)
         client->input[WLC_POINTER] = NULL;
   }

   free(pointer);
}

struct wlc_pointer*
wlc_pointer_new(struct wl_list *clients, struct wl_list *views)
{
   assert(views);

   struct wlc_pointer *pointer;
   if (!(pointer = calloc(1, sizeof(struct wlc_pointer))))
      return NULL;

   pointer->clients = clients;
   pointer->views = views;
   return pointer;
}
