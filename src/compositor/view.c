#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <chck/math/math.h>
#include <wayland-server.h>
#include "internal.h"
#include "view.h"
#include "macros.h"
#include "visibility.h"
#include "output.h"
#include "resources/types/xdg-toplevel.h"
#include "resources/types/xdg-popup.h"
#include "resources/types/xdg-positioner.h"
#include "resources/types/shell-surface.h"
#include "resources/types/surface.h"

static void
surface_update_coordinate_transform(struct wlc_surface *surface, const struct wlc_geometry *area)
{
   assert(area);

   if (!surface || surface->size.w * surface->size.h <= 0)
      return;

   surface->coordinate_transform.w = (float)(area->size.w) / surface->size.w;
   surface->coordinate_transform.h = (float)(area->size.h) / surface->size.h;
}

static void
surface_tree_update_coordinate_transform(struct wlc_surface *surface, const struct wlc_geometry *area)
{
   assert(surface && area);
   surface_update_coordinate_transform(surface, area);

   wlc_resource *s;
   chck_iter_pool_for_each(&surface->subsurface_list, s)
      surface_update_coordinate_transform(convert_from_wlc_resource(*s, "surface"), area);
}

static void
configure_view(struct wlc_view *view, uint32_t edges, const struct wlc_geometry *g)
{
   assert(view && g);

   struct wl_resource *r;
   if (view->xdg_toplevel && (r = wl_resource_from_wlc_resource(view->xdg_toplevel, "xdg-toplevel"))) {
      struct wl_array states = { .size = view->wl_state.items.used, .alloc = view->wl_state.items.allocated, .data = view->wl_state.items.buffer };
      zxdg_toplevel_v6_send_configure(r, g->size.w, g->size.h, &states);
   } else if (view->xdg_popup && (r = wl_resource_from_wlc_resource(view->xdg_popup, "xdg-popup"))) {
      struct wlc_xdg_popup *xdg_popup = convert_from_wlc_resource(view->xdg_popup, "xdg-popup");
      struct wlc_size size = g->size;
      
      if (xdg_popup->xdg_positioner && (xdg_popup->xdg_positioner->flags & WLC_XDG_POSITIONER_HAS_SIZE))
         size = xdg_popup->xdg_positioner->size;
      
      zxdg_popup_v6_send_configure(r, g->origin.x, g->origin.y, size.w, size.h);
   } else if (view->shell_surface && (r = wl_resource_from_wlc_resource(view->shell_surface, "shell-surface"))) {
      wl_shell_surface_send_configure(r, edges, g->size.w, g->size.h);
   } else if (is_x11_view(view)) {
      wlc_x11_window_configure(&view->x11, g);
   }

   if (view->xdg_surface && (r = wl_resource_from_wlc_resource(view->xdg_surface, "xdg-surface")))
      zxdg_surface_v6_send_configure(r, wl_display_next_serial(wlc_display()));
}

void
wlc_view_update(struct wlc_view *view)
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
   configure_view(view, view->pending.edges, &view->pending.geometry);
   wlc_view_commit_state(view, &view->pending, &view->commit);
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
   assert(view && pending && out);

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
         enum wlc_view_state_bit bit;
         uint32_t state;
      } map[] = {
         { WLC_BIT_MAXIMIZED, ZXDG_TOPLEVEL_V6_STATE_MAXIMIZED },
         { WLC_BIT_FULLSCREEN, ZXDG_TOPLEVEL_V6_STATE_FULLSCREEN },
         { WLC_BIT_RESIZING, ZXDG_TOPLEVEL_V6_STATE_RESIZING },
         { WLC_BIT_ACTIVATED, ZXDG_TOPLEVEL_V6_STATE_ACTIVATED },
      };

      chck_iter_pool_flush(&view->wl_state);

      for (uint32_t i = 0; i < LENGTH(map); ++i) {
         if (pending->state & map[i].bit)
            chck_iter_pool_push_back(&view->wl_state, &map[i].state);
      }
   }

   const bool size_changed = (!wlc_size_equals(&pending->geometry.size, &out->geometry.size) || !wlc_size_equals(&pending->geometry.size, &surface->size));
   wlc_dlog(WLC_DBG_COMMIT, "=> pending view commit %" PRIuWLC " (%d) pending: %ux%u commited: %ux%u surface: %ux%u", convert_to_wlc_handle(view), size_changed, pending->geometry.size.w, pending->geometry.size.h, out->geometry.size.w, out->geometry.size.h, surface->size.w, surface->size.h);

   if (pending->state != out->state || size_changed)
      configure_view(view, pending->edges, &pending->geometry);

   *out = *pending;

   struct wlc_geometry geom, visible;
   wlc_view_get_bounds(view, &geom, &visible);
   surface_tree_update_coordinate_transform(surface, &visible);

   wlc_dlog(WLC_DBG_COMMIT, "=> commit view %" PRIuWLC, convert_to_wlc_handle(view));
}

void
wlc_view_ack_surface_attach(struct wlc_view *view, struct wlc_surface *surface)
{
   assert(view && surface);

   if (is_x11_view(view)) {
      surface->pending.opaque.extents = (pixman_box32_t){ 0, 0, surface->size.w, surface->size.h };
      view->surface_pending.visible = (struct wlc_geometry){ wlc_point_zero, surface->size };
   }

   const bool resizing = (view->pending.state & WLC_BIT_RESIZING || view->commit.state & WLC_BIT_RESIZING);
   if (!resizing && !wlc_geometry_equals(&view->surface_pending.visible, &view->surface_commit.visible)) {
      struct wlc_geometry g = (struct wlc_geometry){ view->pending.geometry.origin, view->surface_pending.visible.size };
      wlc_view_request_geometry(view, &g);
   }

   view->surface_commit = view->surface_pending;
}

void
wlc_view_get_bounds(struct wlc_view *view, struct wlc_geometry *out_bounds, struct wlc_geometry *out_visible)
{
   assert(view && out_bounds && out_bounds != out_visible);
   memcpy(out_bounds, &view->commit.geometry, sizeof(struct wlc_geometry));

   struct wlc_surface *surface;
   if (!(surface = convert_from_wlc_resource(view->surface, "surface")))
      return;

   if (view->xdg_toplevel && !wlc_size_equals(&view->surface_commit.visible.size, &wlc_size_zero)) {
      // xdg-surface client that draws drop shadows or other stuff.
      struct wlc_geometry v = view->surface_commit.visible;
      v.origin.x = chck_clamp32(v.origin.x, 0, surface->size.w);
      v.origin.y = chck_clamp32(v.origin.y, 0, surface->size.h);
      v.size.w = chck_clampu32(surface->size.w - v.size.w, 0, surface->size.w);
      v.size.h = chck_clampu32(surface->size.h - v.size.h, 0, surface->size.h);

      assert(surface->size.w > 0 && surface->size.h > 0);
      const float wa = (float)out_bounds->size.w / surface->size.w, ha = (float)out_bounds->size.h / surface->size.h;

      out_bounds->origin.x -= v.origin.x * wa;
      out_bounds->origin.y -= v.origin.y * ha;
      out_bounds->size.w += v.size.w * wa;
      out_bounds->size.h += v.size.h * ha;
   }

   // Make sure bounds is never 0x0 w/h
   wlc_size_max(&out_bounds->size, &(struct wlc_size){ 1, 1 }, &out_bounds->size);

   if (!out_visible)
      return;

   // Actual visible area of the view
   // The idea is to draw black borders to the bounds area, while centering the visible area.
   if ((is_x11_view(view) || view->shell_surface) && !wlc_size_equals(&surface->size, &out_bounds->size)) {
      out_visible->size = surface->size;

      // Scale visible area retaining aspect
      assert(surface->size.w > 0 && surface->size.h > 0);
      const float ba = (float)out_bounds->size.w / (float)out_bounds->size.h;
      const float sa = (float)surface->size.w / (float)surface->size.h;
      if (ba < sa) {
         out_visible->size.w *= (float)out_bounds->size.w / surface->size.w;
         out_visible->size.h *= (float)out_bounds->size.w / surface->size.w;
      } else {
         out_visible->size.w *= (float)out_bounds->size.h / surface->size.h;
         out_visible->size.h *= (float)out_bounds->size.h / surface->size.h;
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

bool
wlc_view_get_opaque(struct wlc_view *view, struct wlc_geometry *out_opaque)
{
   assert(view && out_opaque);

   struct wlc_geometry b, v;
   wlc_view_get_bounds(view, &b, &v);
   return wlc_surface_get_opaque(convert_from_wlc_resource(view->surface, "surface"), &v.origin, out_opaque);
}

void
wlc_view_get_input(struct wlc_view *view, struct wlc_geometry *out_input)
{
   assert(view && out_input);
   struct wlc_geometry b, v;
   wlc_view_get_bounds(view, &b, &v);

   if (is_x11_view(view)) {
      *out_input = v;
      return;
   }

   wlc_surface_get_input(convert_from_wlc_resource(view->surface, "surface"), &v.origin, out_input);
}

bool
wlc_view_request_geometry(struct wlc_view *view, const struct wlc_geometry *r)
{
   assert(view && r);
   wlc_dlog(WLC_DBG_REQUEST, "(%" PRIuWLC ") requested geometry %ux%u+%d,%d", convert_to_wlc_handle(view), r->size.w, r->size.h, r->origin.x, r->origin.y);

   if (view->state.created && wlc_interface()->view.request.geometry) {
      WLC_INTERFACE_EMIT(view.request.geometry, convert_to_wlc_handle(view), r);
   } else {
      memcpy(&view->pending.geometry, r, sizeof(view->pending.geometry));
   }

   configure_view(view, view->pending.edges, &view->pending.geometry);
   wlc_dlog(WLC_DBG_REQUEST, "(%" PRIuWLC ") applied geometry %ux%u+%d,%d", convert_to_wlc_handle(view), view->pending.geometry.size.w, view->pending.geometry.size.h, view->pending.geometry.origin.x, view->pending.geometry.origin.y);
   return wlc_geometry_equals(r, &view->pending.geometry);
}

bool
wlc_view_request_state(struct wlc_view *view, enum wlc_view_state_bit state, bool toggle)
{
   if (!view || !view->state.created)
      return false;

   if (!!(view->pending.state & state) == toggle) {
      // refresh geometry
      if (state == WLC_BIT_FULLSCREEN || state == WLC_BIT_MAXIMIZED)
         configure_view(view, view->pending.edges, &view->pending.geometry);
      return true;
   }

   wlc_dlog(WLC_DBG_REQUEST, "(%" PRIuWLC ") requested state %d", convert_to_wlc_handle(view), state);

   if (wlc_interface()->view.request.state) {
      WLC_INTERFACE_EMIT(view.request.state, convert_to_wlc_handle(view), state, toggle);
   } else {
      wlc_view_set_state_ptr(view, state, toggle);
   }

   wlc_dlog(WLC_DBG_REQUEST, "(%" PRIuWLC ") applied states %d", convert_to_wlc_handle(view), view->pending.state);
   return (!!(view->pending.state & state) == toggle);
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
wlc_view_get_client_ptr(struct wlc_view *view)
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
   wlc_view_update(view);
}

void
wlc_view_set_geometry_ptr(struct wlc_view *view, uint32_t edges, const struct wlc_geometry *geometry)
{
   assert(geometry);

   if (!view)
      return;

   view->pending.geometry = *geometry;
   view->pending.edges = edges;
   wlc_view_update(view);
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

   if (is_x11_view(view))
      wlc_x11_window_set_state(&view->x11, state, toggle);

#define BIT_TOGGLE(w, m, f) (w & ~m) | (-f & m)
   view->pending.state = BIT_TOGGLE(view->pending.state, state, toggle);
#undef BIT_TOGGLE
   wlc_view_update(view);
}

void
wlc_view_set_parent_ptr(struct wlc_view *view, struct wlc_view *parent)
{
   if (!view || view == parent)
      return;

   view->parent = convert_to_wlc_handle(parent);
   wlc_view_update(view);
}

void
wlc_view_set_minimized_ptr(struct wlc_view *view, bool minimized)
{
   if (!view)
      return;

   view->data.minimized = minimized;
}

void
wlc_view_set_title_ptr(struct wlc_view *view, const char *title, size_t length)
{
   if (view && !chck_cstrneq(view->data.title.data, title, length) && chck_string_set_cstr_with_length(&view->data.title, title, length, true)) {
      WLC_INTERFACE_EMIT(view.properties_updated, convert_to_wlc_handle(view), WLC_BIT_PROPERTY_TITLE);
   }
}

void
wlc_view_set_instance_ptr(struct wlc_view *view, const char *instance_, size_t length)
{
   if (view && !chck_cstrneq(view->data._instance.data, instance_, length) && chck_string_set_cstr_with_length(&view->data._instance, instance_, length, true)) {
      WLC_INTERFACE_EMIT(view.properties_updated, convert_to_wlc_handle(view), WLC_BIT_PROPERTY_CLASS);
   }
}

void
wlc_view_set_class_ptr(struct wlc_view *view, const char *class_, size_t length)
{
   if (view && !chck_cstrneq(view->data._class.data, class_, length) && chck_string_set_cstr_with_length(&view->data._class, class_, length, true)) {
      WLC_INTERFACE_EMIT(view.properties_updated, convert_to_wlc_handle(view), WLC_BIT_PROPERTY_CLASS);
   }
}

void
wlc_view_set_app_id_ptr(struct wlc_view *view, const char *app_id)
{
   if (view && !chck_cstreq(view->data.app_id.data, app_id) && chck_string_set_cstr(&view->data.app_id, app_id, true)) {
      WLC_INTERFACE_EMIT(view.properties_updated, convert_to_wlc_handle(view), WLC_BIT_PROPERTY_APP_ID);
   }
}

void
wlc_view_set_pid_ptr(struct wlc_view *view, pid_t pid)
{
   if (!view || view->data.pid == pid)
      return;

   view->data.pid = pid;
   WLC_INTERFACE_EMIT(view.properties_updated, convert_to_wlc_handle(view), WLC_BIT_PROPERTY_PID);
}

void
wlc_view_close_ptr(struct wlc_view *view)
{
   if (!view)
      return;

   struct wl_resource *r;
   if (view->xdg_toplevel && (r = wl_resource_from_wlc_resource(view->xdg_toplevel, "xdg-toplevel"))) {
      zxdg_toplevel_v6_send_close(r);
   } else if (is_x11_view(view)) {
      wlc_x11_window_close(&view->x11);
   } else if (view->xdg_popup && (r = wl_resource_from_wlc_resource(view->xdg_popup, "xdg-popup"))) {
      zxdg_popup_v6_send_popup_done(r);
   } else if (view->shell_surface && (r = wl_resource_from_wlc_resource(view->shell_surface, "shell-surface"))) {
      if (view->type & WLC_BIT_POPUP) {
         wl_shell_surface_send_popup_done(r);
      } else {
         struct wl_client *client = wl_resource_get_client(r);
         wlc_resource_release(view->shell_surface);
         wl_client_destroy(client);
      }
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
   if (view && (view->type & WLC_BIT_UNMANAGED))
      return;

   if (view)
      wlc_x11_window_set_active(&view->x11, true);

   struct wlc_focus_event ev = { .view = view, .type = WLC_FOCUS_EVENT_VIEW };
   wl_signal_emit(&wlc_system_signals()->focus, &ev);
}

static void*
get(struct wlc_view *view, size_t offset)
{
   return (view ? ((char*)view + offset) : NULL);
}

static void*
get_cstr(struct wlc_view *view, size_t offset)
{
   struct chck_string *string = get(view, offset);
   return (view && !chck_string_is_empty(string) ? string->data : NULL);
}

static struct wlc_xdg_positioner*
get_xdg_positioner_for_handle(wlc_handle view)
{
   struct wlc_view *v = convert_from_wlc_handle(view, "view");
   if ( (v) && (v->xdg_popup) && wl_resource_from_wlc_resource(v->xdg_popup, "xdg-popup") ) {
      struct wlc_xdg_popup *popup = convert_from_wlc_resource(v->xdg_popup, "xdg-popup");
      // xdg_positioner object is complete only if it has size and anchor rectangle set
      if ((popup->xdg_positioner) && (popup->xdg_positioner->flags & (WLC_XDG_POSITIONER_HAS_SIZE | WLC_XDG_POSITIONER_HAS_ANCHOR_RECT)))
        return popup->xdg_positioner;
   }
   return NULL;
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

WLC_API const struct wlc_size*
wlc_view_positioner_get_size(wlc_handle view)
{
   struct wlc_xdg_positioner *ps;
   if ((ps = get_xdg_positioner_for_handle(view)))
      return &(ps->size);
   return NULL;
}

WLC_API const struct wlc_geometry*
wlc_view_positioner_get_anchor_rect(wlc_handle view)
{
   struct wlc_xdg_positioner *ps;
   if ((ps = get_xdg_positioner_for_handle(view)))
      return &(ps->anchor_rect);
   return NULL;
}

WLC_API const struct wlc_point*
wlc_view_positioner_get_offset(wlc_handle view)
{
   struct wlc_xdg_positioner *ps;
   if ((ps = get_xdg_positioner_for_handle(view)))
      return &(ps->offset);
   return NULL;
}

WLC_API enum wlc_positioner_anchor_bit
wlc_view_positioner_get_anchor(wlc_handle view)
{
   struct wlc_xdg_positioner *ps;
   if ((ps = get_xdg_positioner_for_handle(view)))
      return ps->anchor;
   return WLC_BIT_ANCHOR_NONE;
}

WLC_API enum wlc_positioner_gravity_bit
wlc_view_positioner_get_gravity(wlc_handle view)
{
   struct wlc_xdg_positioner *ps;
   if ((ps = get_xdg_positioner_for_handle(view)))
      return ps->gravity;
   return WLC_BIT_GRAVITY_NONE;
}

WLC_API enum wlc_positioner_constraint_adjustment_bit
wlc_view_positioner_get_constraint_adjustment(wlc_handle view)
{
   struct wlc_xdg_positioner *ps;
   if ((ps = get_xdg_positioner_for_handle(view)))
      return ps->constraint_adjustment;
   return WLC_BIT_CONSTRAINT_ADJUSTMENT_NONE;
}

WLC_API void
wlc_view_get_visible_geometry(wlc_handle view, struct wlc_geometry *out_geometry)
{
   assert(out_geometry);

   struct wlc_view *v;
   if (!(v = convert_from_wlc_handle(view, "view")))
      return;

   wlc_view_get_bounds(v, out_geometry, NULL);
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

WLC_API const char*
wlc_view_get_instance(wlc_handle view)
{
   return get_cstr(convert_from_wlc_handle(view, "view"), offsetof(struct wlc_view, data._instance));
}

WLC_API const char*
wlc_view_get_class(wlc_handle view)
{
   return get_cstr(convert_from_wlc_handle(view, "view"), offsetof(struct wlc_view, data._class));
}

WLC_API const char*
wlc_view_get_app_id(wlc_handle view)
{
   return get_cstr(convert_from_wlc_handle(view, "view"), offsetof(struct wlc_view, data.app_id));
}

WLC_API pid_t
wlc_view_get_pid(wlc_handle handle)
{
   struct wlc_view *view;
   if(!(view = convert_from_wlc_handle(handle, "view")))
      return 0;

   if (is_x11_view(view)) {
      return view->data.pid;
   } else {
      pid_t pid;
      wl_client_get_credentials(wlc_view_get_client_ptr(view), &pid, NULL, NULL);
      return pid;
   }
}

void
wlc_view_release(struct wlc_view *view)
{
   if (!view)
      return;

   wlc_view_unmap(view);

   wlc_view_set_parent_ptr(view, NULL);
   wlc_resource_release(view->shell_surface);
   wlc_resource_release(view->xdg_toplevel);
   wlc_resource_release(view->xdg_popup);

   chck_string_release(&view->data.title);
   chck_string_release(&view->data._instance);
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
