#include "pointer.h"
#include "macros.h"
#include "client.h"

#include "compositor/view.h"
#include "compositor/output.h"
#include "compositor/surface.h"
#include "compositor/compositor.h"

#include "render/render.h"

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
wlc_pointer_focus(struct wlc_pointer *pointer, uint32_t serial, struct wlc_view *view, struct wlc_origin *out_pos)
{
   assert(pointer);

   struct wlc_origin d;

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

   if (is_valid_view(pointer->focus))
      wl_pointer_send_leave(pointer->focus->client->input[WLC_POINTER], serial, pointer->focus->surface->resource);

   if (is_valid_view(view))
      wl_pointer_send_enter(view->client->input[WLC_POINTER], serial, view->surface->resource, wl_fixed_from_int(d.x), wl_fixed_from_int(d.y));

   if (!view)
      wlc_pointer_set_surface(pointer, NULL, &wlc_origin_zero);

   pointer->focus = view;
   degrab(pointer);
}

void
wlc_pointer_button(struct wlc_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, enum wl_pointer_button_state state)
{
   assert(pointer);

   if (!is_valid_view(pointer->focus))
      return;

   if (state == WL_POINTER_BUTTON_STATE_PRESSED && !pointer->grabbing) {
      pointer->grabbing = true;
      pointer->grab = pointer->pos;
   } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
      degrab(pointer);
   }

   wl_pointer_send_button(pointer->focus->client->input[WLC_POINTER], serial, time, button, state);
}

void
wlc_pointer_scroll(struct wlc_pointer *pointer, uint32_t time, enum wl_pointer_axis axis, double amount)
{
   assert(pointer);

   if (!is_valid_view(pointer->focus))
      return;

   wl_pointer_send_axis(pointer->focus->client->input[WLC_POINTER], time, axis, wl_fixed_from_double(amount));
}

void
wlc_pointer_motion(struct wlc_pointer *pointer, uint32_t serial, uint32_t time, const struct wlc_origin *pos)
{
   assert(pointer);
   memcpy(&pointer->pos, pos, sizeof(pointer->pos));
   struct wlc_view *focused = view_under_pointer(pointer);

   struct wlc_origin d;
   wlc_pointer_focus(pointer, serial, focused, &d);

   wlc_output_schedule_repaint(pointer->compositor->output);

   if (!is_valid_view(focused))
      return;

   wl_pointer_send_motion(focused->client->input[WLC_POINTER], time, wl_fixed_from_int(d.x), wl_fixed_from_int(d.y));

   if (pointer->grabbing) {
      struct wlc_geometry g = focused->pending.geometry;
      int32_t dx = pos->x - pointer->grab.x;
      int32_t dy = pos->y - pointer->grab.y;

      if (pointer->action == WLC_GRAB_ACTION_MOVE) {
         wlc_view_set_state(focused, WLC_BIT_MOVING, true);

         if (focused->compositor->interface.view.request.geometry) {
            focused->compositor->interface.view.request.geometry(focused->compositor, focused, g.origin.x + dx, g.origin.y + dy, g.size.w, g.size.h);
         } else {
            wlc_view_position(focused, g.origin.x + dx, g.origin.y + dy);
         }
      } else if (pointer->action == WLC_GRAB_ACTION_RESIZE) {
         const struct wlc_size min = { 80, 40 };

         if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_LEFT) {
            g.size.w = MAX(min.w, g.size.w - dx);
         } else if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_RIGHT) {
            g.size.w = MAX(min.w, g.size.w + dx);
         }

         if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_TOP) {
            g.size.h = MAX(min.h, g.size.h - dy);
         } else if (pointer->action_edges & WL_SHELL_SURFACE_RESIZE_BOTTOM) {
            g.size.h = MAX(min.h, g.size.h + dy);
         }

         wlc_view_set_state(focused, WLC_BIT_RESIZING, true);
         focused->resizing = pointer->action_edges;
         wlc_view_resize(focused, g.size.w, g.size.h);
      }

      pointer->grab = pointer->pos;
   }
}

void
wlc_pointer_remove_client_for_resource(struct wlc_pointer *pointer, struct wl_resource *resource)
{
   assert(pointer && resource);

   struct wlc_output *output;
   wl_list_for_each(output, &pointer->compositor->outputs, link) {
      struct wlc_space *space;
      wl_list_for_each(space, &output->spaces, link) {
         struct wlc_view *view;
         wl_list_for_each(view, &space->views, link) {
            if (view->client->input[WLC_POINTER] != resource)
               continue;

            if (pointer->focus && pointer->focus->client && pointer->focus->client->input[WLC_POINTER] == resource) {
               view->client->input[WLC_POINTER] = NULL;
               wlc_pointer_focus(pointer, 0, NULL, NULL);
            } else {
               view->client->input[WLC_POINTER] = NULL;
            }

            return;
         }
      }
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
   if (pointer->focus != focused) {
      uint32_t serial = wl_display_next_serial(pointer->compositor->display);
      wlc_pointer_focus(pointer, serial, focused, NULL);
   }

   if (pointer->surface) {
      wlc_render_surface_paint(render, pointer->surface, &(struct wlc_origin){ pointer->pos.x - pointer->tip.x, pointer->pos.y - pointer->tip.y });
   } else if (!pointer->focus || pointer->focus->x11_window) {
      // Show default cursor when no focus and no surface, or if the focused window is x11_window.
      // In x11 you hide cursor with surface that has transparent pixels.
      wlc_render_pointer_paint(render, &pointer->pos);
   }
}

void
wlc_pointer_free(struct wlc_pointer *pointer)
{
   assert(pointer);

   struct wlc_client *client;
   wl_list_for_each(client, &pointer->compositor->clients, link)
      client->input[WLC_POINTER] = NULL;

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
