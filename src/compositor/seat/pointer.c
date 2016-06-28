#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <wayland-server.h>
#include <chck/math/math.h>
#include "internal.h"
#include "pointer.h"
#include "macros.h"
#include "seat.h"
#include "keyboard.h"
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
   wlc_pointer_set_surface(pointer, surface, &(struct wlc_point){ hotspot_x, hotspot_y });
}

static struct wlc_output*
active_output(struct wlc_pointer *pointer)
{
   struct wlc_seat *seat;
   struct wlc_compositor *compositor;
   except((seat = wl_container_of(pointer, seat, pointer)) && (compositor = wl_container_of(seat, compositor, seat)));
   return convert_from_wlc_handle(compositor->active.output, "output");
}

static bool
view_visible(struct wlc_view *view, uint32_t mask)
{
   if (!view)
      return false;

   return (view->mask & mask);
}

static bool
is_inside_view_input_region(struct wlc_pointer *pointer, struct wlc_view *view)
{
   if (!view)
      return false;

   // FIXME: We should handle subsurfaces as well...

   struct wlc_geometry b;
   wlc_view_get_input(view, &b);

   const struct wlc_geometry point = {
      .origin = { .x = pointer->pos.x, .y = pointer->pos.y },
      .size = { .w = 1, .h = 1 }
   };

   return wlc_geometry_contains(&b, &point);
}

static void
find_surface_at_position_recursive(const struct wlc_geometry *point, struct wlc_surface *parent, struct wlc_focused_surface *out)
{
   wlc_resource *sub;
   chck_iter_pool_for_each(&parent->subsurface_list, sub) {
      struct wlc_surface *subsurface;
      if (!(subsurface = convert_from_wlc_resource(*sub, "surface")))
         continue;

      int32_t dx = subsurface->commit.subsurface_position.x * parent->coordinate_transform.w;
      int32_t dy = subsurface->commit.subsurface_position.y * parent->coordinate_transform.h;

      out->offset.x += dx;
      out->offset.y += dy;
      find_surface_at_position_recursive(point, subsurface, out);

      const struct wlc_geometry b = {
         .origin = out->offset,
         .size = subsurface->size
      };

      if (out->id)
         return;

      if (wlc_geometry_contains(&b, point)) {
         out->id = *sub;
         return;
      }

      out->offset.x -= dx;
      out->offset.y -= dy;
   }
}

static bool
surface_under_pointer(struct wlc_pointer *pointer, struct wlc_output *output, struct wlc_focused_surface *out)
{
   assert(pointer && out);

   if (!output)
      return false;

   out->id = 0;

   const struct wlc_geometry point = {
      .origin = { .x = pointer->pos.x, .y = pointer->pos.y },
      .size = { .w = 1, .h = 1 }
   };

   wlc_handle *h;
   chck_iter_pool_for_each_reverse(&output->views, h) {
      struct wlc_view *view;
      if (!(view = convert_from_wlc_handle(*h, "view")) || !view_visible(view, output->active.mask))
         continue;

      struct wlc_geometry b, v;
      wlc_view_get_bounds(view, &b, &v);

      struct wlc_surface *surface;
      if ((surface = convert_from_wlc_resource(view->surface, "surface"))) {
         out->offset = b.origin;
         find_surface_at_position_recursive(&point, surface, out);

         if (out->id) {
            return true;
         } else if (wlc_geometry_contains(&v, &point)) {
            out->id = view->surface;
            return true;
         }
      }
   }

   return false;
}

static void
pointer_paint(struct wlc_pointer *pointer, struct wlc_output *output)
{
   assert(output);

   if (!pointer || output != active_output(pointer))
      return;

   const struct wlc_point pos = {
      chck_clamp(pointer->pos.x, 0, output->resolution.w),
      chck_clamp(pointer->pos.y, 0, output->resolution.h)
   };

   struct wlc_view *view = convert_from_wlc_handle(pointer->focused.view, "view");
   struct wlc_surface *surface;

   if ((surface = convert_from_wlc_resource(pointer->surface, "surface"))) {
      if (surface->output != convert_to_wlc_handle(output) && !wlc_surface_attach_to_output(surface, output, wlc_surface_get_buffer(surface))) {
         // Fallback
         wlc_render_pointer_paint(&output->render, &output->context, &pos);
      } else {
         wlc_output_render_surface(output, surface, &(struct wlc_geometry){ .origin = { pos.x - pointer->tip.x, pos.y - pointer->tip.y }, surface->size }, &output->callbacks);
      }
   } else if (!view || is_x11_view(view)) { // focused->x11.id workarounds bug <https://github.com/Cloudef/wlc/issues/21>
      // Show default cursor when no focus and no surface.
      wlc_render_pointer_paint(&output->render, &output->context, &pos);
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

   struct wl_resource *surface;
   if (!(surface = wl_resource_from_wlc_resource(pointer->focused.surface.id, "surface")))
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
   pointer->focused.surface.id = 0;
   pointer->focused.view = 0;
}

static void
focus_view(struct wlc_pointer *pointer, struct wlc_surface *surf, wlc_handle old_focus, const struct wlc_pointer_origin *pos)
{
   assert(pointer);

   if (!surf || surf->parent_view != old_focus)
        wlc_pointer_set_surface(pointer, NULL, &wlc_point_zero);

   struct wl_resource *surface;
   if (!surf || !(surface = convert_to_wl_resource(surf, "surface")))
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

   pointer->focused.surface.id = convert_to_wlc_resource(surf);
   pointer->focused.view = surf->parent_view;
}

void
wlc_pointer_focus(struct wlc_pointer *pointer, struct wlc_surface *surface, struct wlc_pointer_origin *out_pos)
{
   assert(pointer);

   struct wlc_pointer_origin d = {0, 0};

   if (out_pos)
      memcpy(out_pos, &d, sizeof(d));

   if (surface) {
      d.x = (pointer->pos.x - pointer->focused.surface.offset.x) / surface->coordinate_transform.w;
      d.y = (pointer->pos.y - pointer->focused.surface.offset.y) / surface->coordinate_transform.h;

      d.x = chck_clamp(d.x, 0, surface->size.w);
      d.y = chck_clamp(d.y, 0, surface->size.h);

      if (out_pos)
         memcpy(out_pos, &d, sizeof(d));
   }

   if (pointer->focused.surface.id == convert_to_wlc_resource(surface))
      return;

   wlc_dlog(WLC_DBG_FOCUS, "-> pointer focus event %" PRIuWLC ", %" PRIuWLC, pointer->focused.surface.id, convert_to_wlc_resource(surface));

   wlc_handle old_focused_view = pointer->focused.view;
   defocus(pointer);
   focus_view(pointer, surface, old_focused_view, &d);
}

void
wlc_pointer_button(struct wlc_pointer *pointer, uint32_t time, uint32_t button, enum wl_pointer_button_state state)
{
   assert(pointer);

   struct wlc_seat *seat;
   struct wlc_compositor *compositor;
   except((seat = wl_container_of(pointer, seat, pointer)) && (compositor = wl_container_of(seat, compositor, seat)));

   // Special handling for popups
   if (seat->keyboard.focused.view != pointer->focused.view) {
      struct wlc_view *v;
      if ((v = convert_from_wlc_handle(seat->keyboard.focused.view, "view")) && !is_x11_view(v) && (v->type & WLC_BIT_POPUP)) {
         struct wl_client *client = NULL;

         struct wl_resource *surface;
         if ((surface = wl_resource_from_wlc_resource(v->surface, "surface")))
            client = wl_resource_get_client(surface);

         if (focused_client(pointer) != client) {
            wlc_view_close_ptr(v);
            return;
         }
      }
   }

   if (!is_inside_view_input_region(pointer, convert_from_wlc_handle(pointer->focused.view, "view")))
      return;

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

   if (!is_inside_view_input_region(pointer, convert_from_wlc_handle(pointer->focused.view, "view")))
      return;

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
wlc_pointer_motion(struct wlc_pointer *pointer, uint32_t time, bool pass)
{
   assert(pointer);

   struct wlc_output *output = active_output(pointer);
   struct wlc_focused_surface focused = {0};
   struct wlc_pointer_origin d = {0};

   surface_under_pointer(pointer, output, &focused);
   pointer->focused.surface.offset = focused.offset;

   if (pass)
      wlc_pointer_focus(pointer, convert_from_wlc_resource(focused.id, "surface"), &d);

   wlc_output_schedule_repaint(output);

   if (!focused.id || !pass)
      return;

   if (!is_inside_view_input_region(pointer, convert_from_wlc_handle(pointer->focused.view, "view")))
      return;

   wlc_resource *r;
   chck_iter_pool_for_each(&pointer->focused.resources, r) {
      struct wl_resource *wr;
      if (!(wr = wl_resource_from_wlc_resource(*r, "pointer")))
         continue;

      wl_pointer_send_motion(wr, time, wl_fixed_from_double(d.x), wl_fixed_from_double(d.y));
   }
}

void
wlc_pointer_set_surface(struct wlc_pointer *pointer, struct wlc_surface *surface, const struct wlc_point *tip)
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

   if (pointer->listener.render.notify)
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

WLC_CONST const struct wl_pointer_interface*
wlc_pointer_implementation(void)
{
   static const struct wl_pointer_interface wl_pointer_implementation = {
      .set_cursor = wl_cb_pointer_set_cursor,
      .release = wlc_cb_resource_destructor
   };

   return &wl_pointer_implementation;
}
