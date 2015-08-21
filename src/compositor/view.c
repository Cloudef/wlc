#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <chck/math/math.h>
#include <wayland-server.h>
#include "wayland-xdg-shell-server-protocol.h"
#include "internal.h"
#include "view.h"
#include "macros.h"
#include "visibility.h"
#include "output.h"
#include "resources/types/xdg-surface.h"
#include "resources/types/shell-surface.h"
#include "resources/types/surface.h"

static void
update(struct wlc_view *view)
{
   assert(view);

   if (!memcmp(&view->pending, &view->commit, sizeof(view->commit)))
      return;

   wlc_output_schedule_repaint(wlc_view_get_output_ptr(view));
}

void
wlc_view_map(struct wlc_view *view)
{
   assert(view);

   if (view->state.created)
      return;

   wlc_output_link_view(wlc_view_get_output_ptr(view), view, LINK_ABOVE, NULL);
}

void
wlc_view_unmap(struct wlc_view *view)
{
   assert(view);

   wlc_output_unlink_view(wlc_view_get_output_ptr(view), view);

   if (!view->state.created)
      return;

   WLC_INTERFACE_EMIT(view.destroyed, convert_to_wlc_handle(view));
   view->state.created = false;
}

void
wlc_view_commit_state(struct wlc_view *view, struct wlc_view_state *pending, struct wlc_view_state *out)
{
   struct wlc_surface *surface;
   if (!(surface = convert_from_wlc_resource(view->surface, "surface")))
      return;

   // FIXME: handle ping
#if 0
      struct wl_resource *r;
      if (view->shell_surface && (r = wl_resource_from_wlc_resource(view->shell_surface, "shell-surface")))
         wl_shell_surface_send_ping(r, wl_display_next_serial(wlc_display()));

      wlc_dlog(WLC_DBG_COMMIT, "=> ping view %" PRIuWLC, convert_to_wlc_handle(view));
      return;
#endif

   if (!view->state.created) {
      // Initial size of the view
      view->pending.geometry.size = surface->size;
      view->state.created = true;

      if (WLC_INTERFACE_EMIT_EXCEPT(view.created, false, convert_to_wlc_handle(view))) {
         wlc_view_close_ptr(view);
         return;
      }
   }

   if (!memcmp(pending, out, sizeof(struct wlc_view_state)))
      return;

   if (pending->state != out->state) {
      const struct {
         uint32_t bit;
         uint32_t state;
      } map[] = {
         { WLC_BIT_MAXIMIZED, XDG_SURFACE_STATE_MAXIMIZED },
         { WLC_BIT_FULLSCREEN, XDG_SURFACE_STATE_FULLSCREEN },
         { WLC_BIT_RESIZING, XDG_SURFACE_STATE_RESIZING },
         { WLC_BIT_ACTIVATED, XDG_SURFACE_STATE_ACTIVATED },
         { 0, 0 },
      };

      chck_iter_pool_flush(&view->wl_state);

      for (uint32_t i = 0; map[i].state != 0; ++i) {
         if (pending->state & map[i].bit)
            chck_iter_pool_push_back(&view->wl_state, &map[i].state);
      }
   }

   const bool size_changed = (!wlc_size_equals(&pending->geometry.size, &out->geometry.size) || !wlc_size_equals(&pending->geometry.size, &surface->size));
   wlc_dlog(WLC_DBG_COMMIT, "=> pending commit %" PRIuWLC " (%d) pending: %ux%u commited: %ux%u surface: %ux%u", convert_to_wlc_handle(view), size_changed, pending->geometry.size.w, pending->geometry.size.h, out->geometry.size.w, out->geometry.size.h, surface->size.w, surface->size.h);

   if (pending->state != out->state || size_changed) {
      struct wl_resource *r;
      if (view->xdg_surface && (r = wl_resource_from_wlc_resource(view->xdg_surface, "xdg-surface"))) {
         const uint32_t serial = wl_display_next_serial(wlc_display());
         struct wl_array states = { .size = view->wl_state.items.used, .alloc = view->wl_state.items.allocated, .data = view->wl_state.items.buffer };
         xdg_surface_send_configure(r, pending->geometry.size.w, pending->geometry.size.h, &states, serial);
      } else if (view->shell_surface && (r = wl_resource_from_wlc_resource(view->shell_surface, "shell-surface"))) {
         wl_shell_surface_send_configure(r, pending->edges, pending->geometry.size.w, pending->geometry.size.h);
      }
   }

   if (view->x11.id) {
      if (!wlc_origin_equals(&pending->geometry.origin, &out->geometry.origin))
         wlc_x11_window_position(&view->x11, pending->geometry.origin.x, pending->geometry.origin.y);

      if (size_changed)
         wlc_x11_window_resize(&view->x11, pending->geometry.size.w, pending->geometry.size.h);
   }

   memcpy(out, pending, sizeof(struct wlc_view_state));
   wlc_dlog(WLC_DBG_COMMIT, "=> commit %" PRIuWLC, convert_to_wlc_handle(view));
}

void
wlc_view_ack_surface_attach(struct wlc_view *view, struct wlc_surface *surface, struct wlc_size *old_surface_size)
{
   assert(view && surface && old_surface_size);

   if (view->x11.id)
      surface->pending.opaque.extents = (pixman_box32_t){ 0, 0, surface->size.w, surface->size.h };

   const bool resizing = (view->pending.state & WLC_BIT_RESIZING);
   if (resizing) {
      if (view->pending.edges & WLC_RESIZE_EDGE_LEFT || view->commit.edges & WLC_RESIZE_EDGE_LEFT)
         view->pending.geometry.origin.x += old_surface_size->w - surface->size.w;
      if (view->pending.edges & WLC_RESIZE_EDGE_TOP || view->commit.edges & WLC_RESIZE_EDGE_TOP)
         view->pending.geometry.origin.y += old_surface_size->h - surface->size.h;
   }

}

static bool
should_be_transformed_by_parent(struct wlc_view *view)
{
   return !(view->type & WLC_BIT_OVERRIDE_REDIRECT) && !(view->type & WLC_BIT_UNMANAGED);
}

void
wlc_view_get_bounds(struct wlc_view *view, struct wlc_geometry *out_bounds, struct wlc_geometry *out_visible)
{
   assert(view && out_bounds);
   memcpy(out_bounds, &view->commit.geometry, sizeof(struct wlc_geometry));

   struct wlc_surface *surface;
   if (!(surface = convert_from_wlc_resource(view->surface, "surface")))
      return;

   if (should_be_transformed_by_parent(view)) {
      for (struct wlc_view *parent = convert_from_wlc_handle(view->parent, "view"); parent; parent = convert_from_wlc_handle(parent->parent, "view")) {
         out_bounds->origin.x += parent->commit.geometry.origin.x;
         out_bounds->origin.y += parent->commit.geometry.origin.y;
      }
   }

   if (view->xdg_surface && view->commit.visible.size.w > 0 && view->commit.visible.size.h > 0) {
      // xdg-surface client that draws drop shadows or other stuff.
      // Only obey visible hints when not maximized or fullscreen.
      if (!(view->commit.state & WLC_BIT_MAXIMIZED) && !(view->commit.state & WLC_BIT_FULLSCREEN)) {
         out_bounds->origin.x -= view->commit.visible.origin.x;
         out_bounds->origin.y -= view->commit.visible.origin.y;

         // Make sure size is at least what we want, but may be bigger (shadows etc...)
         out_bounds->size.w = chck_maxu32(surface->size.w, view->commit.geometry.size.w);
         out_bounds->size.h = chck_maxu32(surface->size.h, view->commit.geometry.size.h);
      }
   }

   // Make sure bounds is never 0x0 w/h
   wlc_size_max(&out_bounds->size, &(struct wlc_size){ 1, 1 }, &out_bounds->size);

   if (!out_visible)
      return;

   // Actual visible area of the view
   // The idea is to draw black borders to the bounds area, while centering the visible area.
   if ((view->x11.id || view->shell_surface) && !wlc_size_equals(&surface->size, &out_bounds->size)) {
      out_visible->size = surface->size;

      // Scale visible area retaining aspect
      struct wlc_size ssize;
      wlc_size_max(&surface->size, &(struct wlc_size){ 1, 1 }, &ssize);
      const float ba = (float)out_bounds->size.w / (float)out_bounds->size.h;
      const float sa = (float)ssize.w / (float)ssize.h;
      if (ba < sa) {
         out_visible->size.w *= (float)out_bounds->size.w / ssize.w;
         out_visible->size.h *= (float)out_bounds->size.w / ssize.w;
      } else {
         out_visible->size.w *= (float)out_bounds->size.h / ssize.h;
         out_visible->size.h *= (float)out_bounds->size.h / ssize.h;
      }

      // Center visible area
      out_visible->origin.x = out_bounds->origin.x + out_bounds->size.w * 0.5 - out_visible->size.w * 0.5;
      out_visible->origin.y = out_bounds->origin.y + out_bounds->size.h * 0.5 - out_visible->size.h * 0.5;

      // Make sure visible is never 0x0 w/h
      out_visible->size.w = chck_maxu32(out_visible->size.w, 1);
      out_visible->size.h = chck_maxu32(out_visible->size.h, 1);
   } else {
      // For non wl_shell or x11 surfaces, just memcpy
      memcpy(out_visible, out_bounds, sizeof(struct wlc_geometry));
   }
}

void
wlc_view_get_opaque(struct wlc_view *view, struct wlc_geometry *out_opaque)
{
   assert(view && out_opaque);
   memcpy(out_opaque, &wlc_geometry_zero, sizeof(struct wlc_geometry));

   struct wlc_surface *surface;
   if (!(surface = convert_from_wlc_resource(view->surface, "surface")))
      return;

   struct wlc_geometry b, v;
   wlc_view_get_bounds(view, &b, &v);

   if (wlc_size_equals(&surface->size, &b.size) || wlc_geometry_equals(&v, &b)) {
      // Only ran when we don't draw black borders behind the view
      const float miw = chck_minf(surface->size.w, b.size.w), maw = chck_maxf(surface->size.w, b.size.w);
      const float mih = chck_minf(surface->size.h, b.size.h), mah = chck_maxf(surface->size.h, b.size.h);
      b.origin.x += surface->pending.opaque.extents.x1 * miw / maw;
      b.origin.y += surface->pending.opaque.extents.y1 * mih / mah;
      b.size.w = surface->pending.opaque.extents.x2 * miw / maw;
      b.size.h = surface->pending.opaque.extents.y2 * mih / mah;
      // printf("%ux%u+%d,%d\n", b.size.w, b.size.h, b.origin.x, b.origin.y);
   }

   memcpy(out_opaque, &b, sizeof(b));
}

bool
wlc_view_request_geometry(struct wlc_view *view, const struct wlc_geometry *r)
{
   assert(view && r);
   bool granted = true;

   if (view->state.created && wlc_interface()->view.request.geometry) {
      WLC_INTERFACE_EMIT(view.request.geometry, convert_to_wlc_handle(view), r);

      // User did not follow the request.
      if (!wlc_geometry_equals(r, &view->pending.geometry))
         granted = false;
   } else {
      memcpy(&view->pending.geometry, r, sizeof(view->pending.geometry));
   }

   return granted;
}

void
wlc_view_request_state(struct wlc_view *view, enum wlc_view_state_bit state, bool toggle)
{
   if (!view || !view->state.created || !!(view->pending.state & state) == toggle)
      return;

   WLC_INTERFACE_EMIT(view.request.state, convert_to_wlc_handle(view), state, toggle);
}

void
wlc_view_set_surface(struct wlc_view *view, struct wlc_surface *surface)
{
   if (!view || view->surface == convert_to_wlc_resource(surface))
      return;

   wlc_handle old = view->surface;
   view->surface = convert_to_wlc_resource(surface);
   wlc_surface_attach_to_view(convert_from_wlc_resource(old, "surface"), NULL);
   wlc_surface_attach_to_view(surface, view);

   if (surface && surface->commit.attached) {
      wlc_view_map(view);
   } else {
      wlc_view_unmap(view);
   }

   wlc_dlog(WLC_DBG_RENDER, "-> Linked surface (%" PRIuWLC ") to view (%" PRIuWLC ")", convert_to_wlc_resource(surface), convert_to_wlc_handle(view));
}

struct wl_client*
wlc_view_get_client(struct wlc_view *view)
{
   struct wl_resource *r = (view ? wl_resource_from_wlc_resource(view->surface, "surface") : NULL);
   return (r ? wl_resource_get_client(r) : NULL);
}

struct wlc_output*
wlc_view_get_output_ptr(struct wlc_view *view)
{
   struct wlc_surface *surface;
   if (!view || !(surface = convert_from_wlc_resource(view->surface, "surface")))
      return NULL;

   return convert_from_wlc_handle(surface->output, "output");
}

void
wlc_view_set_output_ptr(struct wlc_view *view, struct wlc_output *output)
{
   if (!view || wlc_view_get_output_ptr(view) == output)
      return;

   wlc_output_link_view(output, view, LINK_ABOVE, NULL);
}

void
wlc_view_set_mask_ptr(struct wlc_view *view, uint32_t mask)
{
   if (!view)
      return;

   view->mask = mask;
   update(view);
}

void
wlc_view_set_geometry_ptr(struct wlc_view *view, uint32_t edges, const struct wlc_geometry *geometry)
{
   assert(geometry);

   if (!view)
      return;

   view->pending.geometry = *geometry;
   view->pending.edges = edges;
   update(view);
}

void
wlc_view_set_type_ptr(struct wlc_view *view, enum wlc_view_type_bit type, bool toggle)
{
   if (!view)
      return;

#define BIT_TOGGLE(w, m, f) (w & ~m) | (-f & m)
   view->type = BIT_TOGGLE(view->type, type, toggle);
#undef BIT_TOGGLE
}

void
wlc_view_set_state_ptr(struct wlc_view *view, enum wlc_view_state_bit state, bool toggle)
{
   if (!view)
      return;

   if (view->x11.id)
      wlc_x11_window_set_state(&view->x11, state, toggle);

#define BIT_TOGGLE(w, m, f) (w & ~m) | (-f & m)
   view->pending.state = BIT_TOGGLE(view->pending.state, state, toggle);
#undef BIT_TOGGLE
   update(view);
}

void
wlc_view_set_parent_ptr(struct wlc_view *view, struct wlc_view *parent)
{
   if (!view || view == parent)
      return;

   view->parent = convert_to_wlc_handle(parent);
   update(view);
}

void
wlc_view_set_minimized_ptr(struct wlc_view *view, bool minimized)
{
   if (!view)
      return;

   view->data.minimized = minimized;
}

bool
wlc_view_set_title_ptr(struct wlc_view *view, const char *title)
{
   return (view ? chck_string_set_cstr(&view->data.title, title, true) : false);
}

bool
wlc_view_set_class_ptr(struct wlc_view *view, const char *class_)
{
   return (view ? chck_string_set_cstr(&view->data._class, class_, true) : false);
}

bool
wlc_view_set_app_id_ptr(struct wlc_view *view, const char *app_id)
{
   return (view ? chck_string_set_cstr(&view->data.app_id, app_id, true) : false);
}

void
wlc_view_close_ptr(struct wlc_view *view)
{
   if (!view)
      return;

   struct wl_resource *r;
   if (view->xdg_surface && (r = wl_resource_from_wlc_resource(view->xdg_surface, "xdg-surface"))) {
      xdg_surface_send_close(r);
   } else if (view->x11.id) {
      wlc_x11_window_close(&view->x11);
   } else if (view->xdg_popup && (r = wl_resource_from_wlc_resource(view->xdg_popup, "xdg-popup"))) {
      xdg_popup_send_popup_done(r);
   } else if (view->shell_surface && (r = wl_resource_from_wlc_resource(view->shell_surface, "shell-surface"))) {
      struct wl_client *client = wl_resource_get_client(r);
      wlc_resource_release(view->shell_surface);
      wl_client_destroy(client);
   }
}

void
wlc_view_send_to(struct wlc_view *view, enum output_link link)
{
   if (!view)
      return;

   wlc_output_link_view(wlc_view_get_output_ptr(view), view, link, NULL);
}

void
wlc_view_send_to_other(struct wlc_view *view, enum output_link link, struct wlc_view *other)
{
   if (!view || other)
      return;

   wlc_output_link_view(wlc_view_get_output_ptr(other), view, link, other);
}

void
wlc_view_focus_ptr(struct wlc_view *view)
{
   bool handled = false;
   if (view && view->x11.id)
      handled = wlc_x11_window_set_active(&view->x11, true);

   if (!handled) {
      struct wlc_focus_event ev = { .view = view, .type = WLC_FOCUS_EVENT_VIEW };
      wl_signal_emit(&wlc_system_signals()->focus, &ev);
   }
}

static void*
get(struct wlc_view *view, size_t offset)
{
   return (view ? ((void*)view + offset) : NULL);
}

static void*
get_cstr(struct wlc_view *view, size_t offset)
{
   struct chck_string *string = ((void*)view + offset);
   return (view && !chck_string_is_empty(string) ? string->data : NULL);
}

WLC_API void
wlc_view_focus(wlc_handle view)
{
   wlc_view_focus_ptr(convert_from_wlc_handle(view, "view"));
}

WLC_API void
wlc_view_close(wlc_handle view)
{
   wlc_view_close_ptr(convert_from_wlc_handle(view, "view"));
}

WLC_API wlc_handle
wlc_view_get_output(wlc_handle view)
{
   return convert_to_wlc_handle(wlc_view_get_output_ptr(convert_from_wlc_handle(view, "view")));
}

WLC_API void
wlc_view_set_output(wlc_handle view, wlc_handle output)
{
   wlc_view_set_output_ptr(convert_from_wlc_handle(view, "view"), convert_from_wlc_handle(output, "output"));
}

WLC_API void
wlc_view_send_to_back(wlc_handle view)
{
   wlc_view_send_to(convert_from_wlc_handle(view, "view"), LINK_BELOW);
}

WLC_API void
wlc_view_send_below(wlc_handle view, wlc_handle other)
{
   wlc_view_send_to_other(convert_from_wlc_handle(view, "view"), LINK_BELOW, convert_from_wlc_handle(other, "view"));
}

WLC_API void
wlc_view_bring_above(wlc_handle view, wlc_handle other)
{
   wlc_view_send_to_other(convert_from_wlc_handle(view, "view"), LINK_ABOVE, convert_from_wlc_handle(other, "view"));
}

WLC_API void
wlc_view_bring_to_front(wlc_handle view)
{
   wlc_view_send_to(convert_from_wlc_handle(view, "view"), LINK_ABOVE);
}

WLC_API uint32_t
wlc_view_get_mask(wlc_handle view)
{
   void *ptr = get(convert_from_wlc_handle(view, "view"), offsetof(struct wlc_view, mask));
   return (ptr ? *(uint32_t*)ptr : 0);
}

WLC_API void
wlc_view_set_mask(wlc_handle view, uint32_t mask)
{
   wlc_view_set_mask_ptr(convert_from_wlc_handle(view, "view"), mask);
}

WLC_API const struct wlc_geometry*
wlc_view_get_geometry(wlc_handle view)
{
   return get(convert_from_wlc_handle(view, "view"), offsetof(struct wlc_view, pending.geometry));
}

WLC_API void
wlc_view_set_geometry(wlc_handle view, uint32_t edges, const struct wlc_geometry *geometry)
{
   wlc_view_set_geometry_ptr(convert_from_wlc_handle(view, "view"), edges, geometry);
}

WLC_API uint32_t
wlc_view_get_type(wlc_handle view)
{
   void *ptr = get(convert_from_wlc_handle(view, "view"), offsetof(struct wlc_view, type));
   return (ptr ? *(uint32_t*)ptr : 0);
}

WLC_API void
wlc_view_set_type(wlc_handle view, enum wlc_view_type_bit type, bool toggle)
{
   wlc_view_set_type_ptr(convert_from_wlc_handle(view, "view"), type, toggle);
}

WLC_API uint32_t
wlc_view_get_state(wlc_handle view)
{
   void *ptr = get(convert_from_wlc_handle(view, "view"), offsetof(struct wlc_view, pending.state));
   return (ptr ? *(uint32_t*)ptr : 0);
}

WLC_API void
wlc_view_set_state(wlc_handle view, enum wlc_view_state_bit state, bool toggle)
{
   wlc_view_set_state_ptr(convert_from_wlc_handle(view, "view"), state, toggle);
}

WLC_API wlc_handle
wlc_view_get_parent(wlc_handle view)
{
   void *ptr = get(convert_from_wlc_handle(view, "view"), offsetof(struct wlc_view, parent));
   return (ptr ? *(wlc_handle*)ptr : 0);
}

WLC_API void
wlc_view_set_parent(wlc_handle view, wlc_handle parent)
{
   wlc_view_set_parent_ptr(convert_from_wlc_handle(view, "view"), convert_from_wlc_handle(parent, "parent"));
}

WLC_API const char*
wlc_view_get_title(wlc_handle view)
{
   return get_cstr(convert_from_wlc_handle(view, "view"), offsetof(struct wlc_view, data.title));
}

WLC_API bool
wlc_view_set_title(wlc_handle view, const char *title)
{
   return wlc_view_set_title_ptr(convert_from_wlc_handle(view, "view"), title);
}

WLC_API const char*
wlc_view_get_class(wlc_handle view)
{
   return get_cstr(convert_from_wlc_handle(view, "view"), offsetof(struct wlc_view, data._class));
}

WLC_API bool
wlc_view_set_class(wlc_handle view, const char *class_)
{
   return wlc_view_set_class_ptr(convert_from_wlc_handle(view, "view"), class_);
}

WLC_API const char*
wlc_view_get_app_id(wlc_handle view)
{
   return get_cstr(convert_from_wlc_handle(view, "view"), offsetof(struct wlc_view, data.app_id));
}

WLC_API bool
wlc_view_set_app_id(wlc_handle view, const char *app_id)
{
   return wlc_view_set_app_id_ptr(convert_from_wlc_handle(view, "view"), app_id);
}

void
wlc_view_release(struct wlc_view *view)
{
   if (!view)
      return;

   wlc_view_unmap(view);

   wlc_view_set_parent_ptr(view, NULL);
   wlc_resource_release(view->shell_surface);
   wlc_resource_release(view->xdg_surface);
   wlc_resource_release(view->xdg_popup);

   chck_string_release(&view->data.title);
   chck_string_release(&view->data._class);
   chck_string_release(&view->data.app_id);

   wlc_surface_attach_to_view(convert_from_wlc_resource(view->surface, "surface"), NULL);
   chck_iter_pool_release(&view->wl_state);
}

bool
wlc_view(struct wlc_view *view)
{
   assert(view);
   assert(!view->state.created);
   return chck_iter_pool(&view->wl_state, 8, 0, sizeof(uint32_t));
}
