#include "internal.h"
#include "pointer.h"
#include "macros.h"

#include "compositor/view.h"
#include "compositor/client.h"
#include "compositor/output.h"
#include "compositor/surface.h"
#include "compositor/compositor.h"

#include "platform/render/render.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <wayland-server.h>

static bool
is_valid_view(const struct wlc_view *view)
{
   return (view && view->client && view->client->input[WLC_POINTER] && view->surface && view->surface->resource);
}

static struct wlc_view*
view_under_pointer(struct wlc_pointer *pointer)
{
   if (!pointer->compositor->output)
      return NULL;

   if (pointer->focus && pointer->grabbing)
      return pointer->focus;

   struct wlc_view *view;
   wl_list_for_each_reverse(view, &pointer->compositor->output->space->views, link) {
      struct wlc_geometry b;
      wlc_view_get_bounds(view, &b, NULL);
      if (pointer->pos.x >= b.origin.x && pointer->pos.x <= b.origin.x + (int32_t)b.size.w &&
          pointer->pos.y >= b.origin.y && pointer->pos.y <= b.origin.y + (int32_t)b.size.h) {
         return view;
      }
   }

   return NULL;
}

static void
degrab(struct wlc_pointer *pointer)
{
   if (pointer->focus && pointer->grabbing) {
      switch (pointer->action) {
         case WLC_GRAB_ACTION_MOVE:
            wlc_view_set_state(pointer->focus, WLC_BIT_MOVING, false);
            break;
         case WLC_GRAB_ACTION_RESIZE:
            wlc_view_set_state(pointer->focus, WLC_BIT_RESIZING, false);
            break;
         default: break;
      }
      pointer->focus->resizing = 0;
   }

   pointer->grabbing = false;
   pointer->action = WLC_GRAB_ACTION_NONE;
   pointer->action_edges = 0;
}

void
wlc_pointer_focus(struct wlc_pointer *pointer, struct wlc_view *view, struct wlc_pointer_origin *out_pos)
{
   assert(pointer);

   struct wlc_pointer_origin d;

   if (view) {
      struct wlc_geometry b, v;
      wlc_view_get_bounds(view, &b, &v);
      d.x = (pointer->pos.x - v.origin.x) * view->surface->size.w / v.size.w;
      d.y = (pointer->pos.y - v.origin.y) * view->surface->size.h / v.size.h;

      if (out_pos)
         memcpy(out_pos, &d, sizeof(d));
   }

   if (pointer->focus == view)
      return;

   struct wl_resource *focused = (is_valid_view(pointer->focus) ? pointer->focus->client->input[WLC_POINTER] : NULL);
   struct wl_resource *focus = (is_valid_view(view) ? view->client->input[WLC_POINTER] : NULL);

   wlc_dlog(WLC_DBG_FOCUS, "-> pointer focus event %p, %p", focused, focus);

   if (focused) {
      uint32_t serial = wl_display_next_serial(wlc_display());
      wl_pointer_send_leave(focused, serial, pointer->focus->surface->resource);
   }

   if (focus) {
      uint32_t serial = wl_display_next_serial(wlc_display());
      wl_pointer_send_enter(focus, serial, view->surface->resource, wl_fixed_from_double(d.x), wl_fixed_from_double(d.y));
   }

   if (!view)
      wlc_pointer_set_surface(pointer, NULL, &wlc_origin_zero);

   pointer->focus = view;
   degrab(pointer);
}

void
wlc_pointer_button(struct wlc_pointer *pointer, uint32_t time, uint32_t button, enum wl_pointer_button_state state)
{
   assert(pointer);

   if (!is_valid_view(pointer->focus))
      return;

   if (state == WL_POINTER_BUTTON_STATE_PRESSED && !pointer->grabbing) {
      pointer->grabbing = true;
      pointer->grab = (struct wlc_origin){ pointer->pos.x, pointer->pos.y };
   } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
      degrab(pointer);
   }

   uint32_t serial = wl_display_next_serial(wlc_display());
   wl_pointer_send_button(pointer->focus->client->input[WLC_POINTER], serial, time, button, state);
}

void
wlc_pointer_scroll(struct wlc_pointer *pointer, uint32_t time, uint8_t axis_bits, double amount[2])
{
   assert(pointer);

   if (!is_valid_view(pointer->focus))
      return;

   if (axis_bits & WLC_SCROLL_AXIS_VERTICAL)
      wl_pointer_send_axis(pointer->focus->client->input[WLC_POINTER], time, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(amount[0]));

   if (axis_bits & WLC_SCROLL_AXIS_HORIZONTAL)
      wl_pointer_send_axis(pointer->focus->client->input[WLC_POINTER], time, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(amount[1]));
}

void
wlc_pointer_motion(struct wlc_pointer *pointer, uint32_t time, const struct wlc_pointer_origin *pos)
{
   assert(pointer);
   memcpy(&pointer->pos, pos, sizeof(pointer->pos));
   struct wlc_view *focused = view_under_pointer(pointer);

   struct wlc_pointer_origin d;
   wlc_pointer_focus(pointer, focused, &d);

   if (pointer->compositor->output)
      wlc_output_schedule_repaint(pointer->compositor->output);

   if (!is_valid_view(focused))
      return;

   wl_pointer_send_motion(focused->client->input[WLC_POINTER], time, wl_fixed_from_double(d.x), wl_fixed_from_double(d.y));

   if (pointer->grabbing) {
      struct wlc_geometry g = focused->pending.geometry;
      int32_t dx = pos->x - pointer->grab.x;
      int32_t dy = pos->y - pointer->grab.y;

      if (pointer->action == WLC_GRAB_ACTION_MOVE) {
         wlc_view_set_state(focused, WLC_BIT_MOVING, true);
         g.origin.x += dx;
         g.origin.y += dy;

         if (wlc_interface()->view.request.geometry) {
            WLC_INTERFACE_EMIT(view.request.geometry, focused->compositor, focused, &g);
         } else {
            wlc_view_set_geometry(focused, &g);
         }
      } else if (pointer->action == WLC_GRAB_ACTION_RESIZE) {
         const struct wlc_size min = { 80, 40 };

         if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_LEFT) {
            g.size.w = fmax(min.w, g.size.w - dx);
         } else if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_RIGHT) {
            g.size.w = fmax(min.w, g.size.w + dx);
         }

         if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_TOP) {
            g.size.h = fmax(min.h, g.size.h - dy);
         } else if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_BOTTOM) {
            g.size.h = fmax(min.h, g.size.h + dy);
         }

         wlc_view_set_state(focused, WLC_BIT_RESIZING, true);
         focused->resizing = pointer->action_edges;
         wlc_view_set_geometry(focused, &g);
      }

      pointer->grab = (struct wlc_origin){ pointer->pos.x, pointer->pos.y };
   }
}

void
wlc_pointer_touch(struct wlc_pointer *pointer, uint32_t time, enum wlc_touch_type type, int32_t slot, const struct wlc_origin *pos)
{
   assert(pointer);

   struct wlc_view *focused = view_under_pointer(pointer);

   if (type == WLC_TOUCH_MOTION || type == WLC_TOUCH_DOWN) {
      memcpy(&pointer->pos, pos, sizeof(pointer->pos));

      struct wlc_pointer_origin d;
      wlc_pointer_focus(pointer, focused, &d);

      if (pointer->compositor->output)
         wlc_output_schedule_repaint(pointer->compositor->output);
   }

   if (!is_valid_view(focused))
      return;

   switch (type) {
      case WLC_TOUCH_DOWN:
         {
            uint32_t serial = wl_display_next_serial(wlc_display());
            wl_touch_send_down(focused->client->input[WLC_TOUCH], serial, time, focused->surface->resource, slot, wl_fixed_from_double(pos->x), wl_fixed_from_double(pos->y));
         }
         break;

      case WLC_TOUCH_UP:
         {
            uint32_t serial = wl_display_next_serial(wlc_display());
            wl_touch_send_up(focused->client->input[WLC_TOUCH], serial, time, slot);
         }
         break;

      case WLC_TOUCH_MOTION:
         wl_touch_send_motion(focused->client->input[WLC_TOUCH], time, slot, wl_fixed_from_double(pos->x), wl_fixed_from_double(pos->y));
         break;

      case WLC_TOUCH_FRAME:
         wl_touch_send_frame(focused->client->input[WLC_TOUCH]);
         break;

      case WLC_TOUCH_CANCEL:
         wl_touch_send_cancel(focused->client->input[WLC_TOUCH]);
         break;
   }
}

void
wlc_pointer_remove_client_for_resource(struct wlc_pointer *pointer, struct wl_resource *resource)
{
   assert(pointer && resource);

   struct wlc_client *client;
   wl_list_for_each(client, &pointer->compositor->clients, link) {
      if (client->input[WLC_POINTER] != resource)
         continue;

      if (pointer->focus && pointer->focus->client && pointer->focus->client->input[WLC_POINTER] == resource) {
         client->input[WLC_POINTER] = NULL;
         wlc_pointer_focus(pointer, NULL, NULL);
      } else {
         client->input[WLC_POINTER] = NULL;
      }

      return;
   }
}

void
wlc_pointer_set_surface(struct wlc_pointer *pointer, struct wlc_surface *surface, const struct wlc_origin *tip)
{
   assert(pointer);

   memcpy(&pointer->tip, tip, sizeof(pointer->tip));

   if (pointer->surface)
      wlc_surface_invalidate(pointer->surface);

   if ((pointer->surface = surface))
      wlc_surface_attach_to_output(surface, pointer->compositor->output, surface->commit.buffer);
}

void
wlc_pointer_paint(struct wlc_pointer *pointer, struct wlc_render *render)
{
   assert(pointer);

   // Skip draw if surface is not on same context.
   // XXX: Should we draw default instead?
   if (pointer->surface && (!pointer->surface->output || pointer->surface->output->render != render))
      return;

   // XXX: Do this check for now every render loop.
   // Maybe later we may do something nicer, like if any view moved or
   // geometry changed then update pointer.
   struct wlc_view *focused = view_under_pointer(pointer);
   if (pointer->focus != focused)
      wlc_pointer_focus(pointer, focused, NULL);

   if (pointer->surface) {
      wlc_render_surface_paint(render, pointer->surface, &(struct wlc_origin){ pointer->pos.x - pointer->tip.x, pointer->pos.y - pointer->tip.y });
   } else if (!pointer->focus || pointer->focus->x11_window) {
      // Show default cursor when no focus and no surface, or if the focused window is x11_window.
      // In x11 you hide cursor with surface that has transparent pixels.
      wlc_render_pointer_paint(render, &(struct wlc_origin){ pointer->pos.x, pointer->pos.y });
   }
}

void
wlc_pointer_free(struct wlc_pointer *pointer)
{
   assert(pointer);

   if (pointer->compositor) {
      struct wlc_client *client;
      wl_list_for_each(client, &pointer->compositor->clients, link) {
         if (!client->input[WLC_POINTER])
            continue;

         wl_resource_destroy(client->input[WLC_POINTER]);
         client->input[WLC_POINTER] = NULL;
      }
   }

   free(pointer);
}

struct wlc_pointer*
wlc_pointer_new(struct wlc_compositor *compositor)
{
   assert(compositor);

   struct wlc_pointer *pointer;
   if (!(pointer = calloc(1, sizeof(struct wlc_pointer))))
      return NULL;

   pointer->compositor = compositor;
   return pointer;
}
