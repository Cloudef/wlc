#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <wayland-server.h>
#include <chck/math/math.h>
#include "internal.h"
#include "pointer.h"
#include "macros.h"
#include "seat.h"
#include "compositor/compositor.h"
#include "compositor/view.h"
#include "compositor/output.h"
#include "resources/types/surface.h"

static struct wl_client*
focused_client(struct wlc_pointer *pointer)
{
   assert(pointer);

   wlc_resource *r;
   struct wl_resource *wr = NULL;
   chck_iter_pool_for_each(&pointer->focused.resources, r) {
      if ((wr = wl_resource_from_wlc_resource(*r, "pointer")))
         break;
   }

   return (wr ? wl_resource_get_client(wr) : NULL);
}

static void
wl_cb_pointer_set_cursor(struct wl_client *client, struct wl_resource *resource, uint32_t serial, struct wl_resource *surface_resource, int32_t hotspot_x, int32_t hotspot_y)
{
   (void)serial;

   struct wlc_pointer *pointer;
   if (!(pointer = wl_resource_get_user_data(resource)))
      return;

   // Only accept request if we happen to have focus on the client.
   if (focused_client(pointer) != client)
      return;

   struct wlc_surface *surface = convert_from_wl_resource(surface_resource, "surface");
   wlc_pointer_set_surface(pointer, surface, &(struct wlc_origin){ hotspot_x, hotspot_y });
}

const struct wl_pointer_interface wl_pointer_implementation = {
   .set_cursor = wl_cb_pointer_set_cursor,
   .release = wlc_cb_resource_destructor
};

static struct wlc_output*
active_output(struct wlc_pointer *pointer)
{
   struct wlc_seat *seat;
   struct wlc_compositor *compositor;
   except((seat = wl_container_of(pointer, seat, pointer)) && (compositor = wl_container_of(seat, compositor, seat)));
   return convert_from_wlc_handle(compositor->active.output, "output");
}

static struct wlc_view*
view_under_pointer(struct wlc_pointer *pointer, struct wlc_output *output)
{
   assert(pointer);

   if (!output)
      return NULL;

   struct wlc_view *view;
   if ((view = convert_from_wlc_handle(pointer->focused.view, "view")) && pointer->state.grabbing)
      return view;

   wlc_handle *h;
   chck_iter_pool_for_each_reverse(&output->views, h) {
      if (!(view = convert_from_wlc_handle(*h, "view")))
         continue;

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
   struct wlc_view *view;
   if (pointer->state.grabbing && (view = convert_from_wlc_handle(pointer->focused.view, "view"))) {
      switch (pointer->state.action) {
         case WLC_GRAB_ACTION_MOVE:
            wlc_view_set_state_ptr(view, WLC_BIT_MOVING, false);
            break;
         case WLC_GRAB_ACTION_RESIZE:
            wlc_view_set_state_ptr(view, WLC_BIT_RESIZING, false);
            break;
         default: break;
      }

      view->state.resizing = 0;
   }

   memset(&pointer->state, 0, sizeof(pointer->state));
}

static void
pointer_paint(struct wlc_pointer *pointer, struct wlc_output *output)
{
   assert(output);

   if (!pointer || output != active_output(pointer))
      return;

   // XXX: Do this check for now every render loop.
   // Maybe later we may do something nicer, like if any view moved or
   // geometry changed then update pointer.
   struct wlc_view *focus = convert_from_wlc_handle(pointer->focused.view, "view");
   struct wlc_view *focused = view_under_pointer(pointer, output);
   if (focus != focused)
      wlc_pointer_focus(pointer, focused, NULL);

   struct wlc_surface *surface;
   if ((surface = convert_from_wlc_resource(pointer->surface, "surface"))) {
      if (surface->output != convert_to_wlc_handle(output) && !wlc_surface_attach_to_output(surface, output, wlc_surface_get_buffer(surface))) {
         // Fallback
         wlc_render_pointer_paint(&output->render, &output->context, &(struct wlc_origin){ pointer->pos.x, pointer->pos.y });
      } else {
         wlc_render_surface_paint(&output->render, &output->context, surface, &(struct wlc_origin){ pointer->pos.x - pointer->tip.x, pointer->pos.y - pointer->tip.y });
      }
   } else if (!focused || focused->x11.id) { // focused->x11.id workarounds bug <https://github.com/Cloudef/wlc/issues/21>
      // Show default cursor when no focus and no surface.
      wlc_render_pointer_paint(&output->render, &output->context, &(struct wlc_origin){ pointer->pos.x, pointer->pos.y });
   }
}

static void
render_event(struct wl_listener *listener, void *data)
{
   struct wlc_pointer *pointer;
   except(pointer = wl_container_of(listener, pointer, listener.render));

   struct wlc_render_event *ev = data;
   switch (ev->type) {
      case WLC_RENDER_EVENT_POINTER:
         pointer_paint(pointer, ev->output);
         break;

      default: break;
   }
}

static void
defocus(struct wlc_pointer *pointer)
{
   assert(pointer);

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle(pointer->focused.view, "view")))
      goto out;

   struct wl_resource *surface;
   if (!(surface = wl_resource_from_wlc_resource(view->surface, "surface")))
      goto out;

   wlc_resource *r;
   chck_iter_pool_for_each(&pointer->focused.resources, r) {
      struct wl_resource *wr;
      if (!(wr = wl_resource_from_wlc_resource(*r, "pointer")))
         continue;

      uint32_t serial = wl_display_next_serial(wlc_display());
      wl_pointer_send_leave(wr, serial, surface);
   }

out:
   chck_iter_pool_flush(&pointer->focused.resources);
   pointer->focused.view = 0;
   wlc_pointer_set_surface(pointer, NULL, &wlc_origin_zero);
}

static void
focus_view(struct wlc_pointer *pointer, struct wlc_view *view, const struct wlc_pointer_origin *pos)
{
   assert(pointer);

   struct wl_resource *surface;
   if (!view || !(surface = wl_resource_from_wlc_resource(view->surface, "surface")))
      return;

   struct wl_client *client = wl_resource_get_client(surface);
   wlc_resource *r;
   chck_pool_for_each(&pointer->resources.pool, r) {
      struct wl_resource *wr;
      if (!(wr = wl_resource_from_wlc_resource(*r, "pointer")) || wl_resource_get_client(wr) != client)
         continue;

      if (!chck_iter_pool_push_back(&pointer->focused.resources, r))
         wlc_log(WLC_LOG_WARN, "Failed to push focused pointer resource to pool (out of memory?)");

      uint32_t serial = wl_display_next_serial(wlc_display());
      wl_pointer_send_enter(wr, serial, surface, wl_fixed_from_double(pos->x), wl_fixed_from_double(pos->y));
   }

   pointer->focused.view = convert_to_wlc_handle(view);
}

void
wlc_pointer_focus(struct wlc_pointer *pointer, struct wlc_view *view, struct wlc_pointer_origin *out_pos)
{
   assert(pointer);

   struct wlc_pointer_origin d = {0, 0};

   if (out_pos)
      memcpy(out_pos, &d, sizeof(d));

   if (view) {
      struct wlc_geometry b, v;
      wlc_view_get_bounds(view, &b, &v);

      struct wlc_surface *s;
      if (!(s = convert_from_wlc_resource(view->surface, "surface")))
         return;

      d.x = (pointer->pos.x - v.origin.x) * (float)s->size.w / v.size.w;
      d.y = (pointer->pos.y - v.origin.y) * (float)s->size.h / v.size.h;
      d.x = chck_clamp(d.x, 0, s->size.w);
      d.y = chck_clamp(d.y, 0, s->size.h);

      if (out_pos)
         memcpy(out_pos, &d, sizeof(d));
   }

   if (pointer->focused.view == convert_to_wlc_handle(view))
      return;

   wlc_dlog(WLC_DBG_FOCUS, "-> pointer focus event %zu, %zu", pointer->focused.view, convert_to_wlc_handle(view));

   defocus(pointer);
   focus_view(pointer, view, &d);
   degrab(pointer);
}

void
wlc_pointer_button(struct wlc_pointer *pointer, uint32_t time, uint32_t button, enum wl_pointer_button_state state)
{
   assert(pointer);

   if (state == WL_POINTER_BUTTON_STATE_PRESSED && !pointer->state.grabbing) {
      pointer->state.grabbing = true;
      pointer->state.grab = (struct wlc_origin){ pointer->pos.x, pointer->pos.y };
   } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
      degrab(pointer);
   }

   wlc_resource *r;
   chck_iter_pool_for_each(&pointer->focused.resources, r) {
      struct wl_resource *wr;
      if (!(wr = wl_resource_from_wlc_resource(*r, "pointer")))
         continue;

      uint32_t serial = wl_display_next_serial(wlc_display());
      wl_pointer_send_button(wr, serial, time, button, state);
   }
}

void
wlc_pointer_scroll(struct wlc_pointer *pointer, uint32_t time, uint8_t axis_bits, double amount[2])
{
   assert(pointer);

   wlc_resource *r;
   chck_iter_pool_for_each(&pointer->focused.resources, r) {
      struct wl_resource *wr;
      if (!(wr = wl_resource_from_wlc_resource(*r, "pointer")))
         continue;

      if (axis_bits & WLC_SCROLL_AXIS_VERTICAL)
         wl_pointer_send_axis(wr, time, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(amount[0]));

      if (axis_bits & WLC_SCROLL_AXIS_HORIZONTAL)
         wl_pointer_send_axis(wr, time, WL_POINTER_AXIS_HORIZONTAL_SCROLL, wl_fixed_from_double(amount[1]));
   }
}

void
wlc_pointer_motion(struct wlc_pointer *pointer, uint32_t time, const struct wlc_pointer_origin *pos)
{
   assert(pointer);
   memcpy(&pointer->pos, pos, sizeof(pointer->pos));

   struct wlc_output *output = active_output(pointer);
   struct wlc_view *focused = view_under_pointer(pointer, output);

   struct wlc_pointer_origin d;
   wlc_pointer_focus(pointer, focused, &d);
   wlc_output_schedule_repaint(output);

   if (!focused)
      return;

   wlc_resource *r;
   chck_iter_pool_for_each(&pointer->focused.resources, r) {
      struct wl_resource *wr;
      if (!(wr = wl_resource_from_wlc_resource(*r, "pointer")))
         continue;

      wl_pointer_send_motion(wr, time, wl_fixed_from_double(d.x), wl_fixed_from_double(d.y));
   }

   if (pointer->state.grabbing) {
      struct wlc_geometry g = focused->pending.geometry;
      int32_t dx = pos->x - pointer->state.grab.x;
      int32_t dy = pos->y - pointer->state.grab.y;

      if (pointer->state.action == WLC_GRAB_ACTION_MOVE) {
         wlc_view_set_state_ptr(focused, WLC_BIT_MOVING, true);
         g.origin.x += dx;
         g.origin.y += dy;

         if (wlc_interface()->view.request.geometry) {
            WLC_INTERFACE_EMIT(view.request.geometry, convert_to_wlc_handle(focused), &g);
         } else {
            wlc_view_set_geometry_ptr(focused, &g);
         }
      } else if (pointer->state.action == WLC_GRAB_ACTION_RESIZE) {
         const struct wlc_size min = { 80, 40 };

         if (pointer->state.action_edges & WL_SHELL_SURFACE_RESIZE_LEFT) {
            g.size.w = chck_maxu32(min.w, g.size.w - dx);
         } else if (pointer->state.action_edges & WL_SHELL_SURFACE_RESIZE_RIGHT) {
            g.size.w = chck_maxu32(min.w, g.size.w + dx);
         }

         if (pointer->state.action_edges & WL_SHELL_SURFACE_RESIZE_TOP) {
            g.size.h = chck_maxu32(min.h, g.size.h - dy);
         } else if (pointer->state.action_edges & WL_SHELL_SURFACE_RESIZE_BOTTOM) {
            g.size.h = chck_maxu32(min.h, g.size.h + dy);
         }

         wlc_view_set_state_ptr(focused, WLC_BIT_RESIZING, true);
         focused->state.resizing = pointer->state.action_edges;
         wlc_view_set_geometry_ptr(focused, &g);
      }

      pointer->state.grab = (struct wlc_origin){ pointer->pos.x, pointer->pos.y };
   }
}

void
wlc_pointer_set_surface(struct wlc_pointer *pointer, struct wlc_surface *surface, const struct wlc_origin *tip)
{
   assert(pointer);
   memcpy(&pointer->tip, tip, sizeof(pointer->tip));
   wlc_surface_invalidate(convert_from_wlc_resource(pointer->surface, "surface"));
   pointer->surface = convert_to_wlc_resource(surface);
}

void
wlc_pointer_release(struct wlc_pointer *pointer)
{
   if (!pointer)
      return;

   wl_list_remove(&pointer->listener.render.link);
   chck_iter_pool_release(&pointer->focused.resources);
   wlc_source_release(&pointer->resources);
   memset(pointer, 0, sizeof(struct wlc_pointer));
}

bool
wlc_pointer(struct wlc_pointer *pointer)
{
   assert(pointer);
   memset(pointer, 0, sizeof(struct wlc_pointer));
   pointer->listener.render.notify = render_event;
   wl_signal_add(&wlc_system_signals()->render, &pointer->listener.render);

   if (!chck_iter_pool(&pointer->focused.resources, 4, 0, sizeof(wlc_resource)))
      goto fail;

   if (!wlc_source(&pointer->resources, "pointer", NULL, NULL, 32, sizeof(struct wlc_resource)))
      goto fail;

   return true;

fail:
   wlc_pointer_release(pointer);
   return false;
}
