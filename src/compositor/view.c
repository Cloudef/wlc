#include "internal.h"
#include "view.h"
#include "macros.h"
#include "visibility.h"
#include "compositor.h"
#include "output.h"
#include "client.h"
#include "surface.h"

#include "shell/surface.h"
#include "shell/xdg-surface.h"

#include "seat/seat.h"
#include "seat/pointer.h"

#include "xwayland/xwm.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <wayland-server.h>
#include "wayland-xdg-shell-server-protocol.h"

static void
update(struct wlc_view *view)
{
   assert(view);

   if (!view->space || !memcmp(&view->pending, &view->commit, sizeof(view->commit)))
      return;

   wlc_output_schedule_repaint(view->space->output);
}

void
wlc_view_commit_state(struct wlc_view *view, struct wlc_view_state *pending, struct wlc_view_state *out)
{
   if (view->ack != ACK_NONE)
      return;

   if (pending->state != out->state) {
      struct {
         uint32_t bit;
         uint32_t state;
      } map[] = {
         { WLC_BIT_MAXIMIZED, XDG_SURFACE_STATE_MAXIMIZED },
         { WLC_BIT_FULLSCREEN, XDG_SURFACE_STATE_FULLSCREEN },
         { WLC_BIT_RESIZING, XDG_SURFACE_STATE_RESIZING },
         { WLC_BIT_ACTIVATED, XDG_SURFACE_STATE_ACTIVATED },
         { 0, 0 },
      };

      wl_array_release(&view->wl_state);
      wl_array_init(&view->wl_state);

      for (unsigned int i = 0; map[i].state != 0; ++i) {
         if (pending->state & map[i].bit) {
            uint32_t *s = wl_array_add(&view->wl_state, sizeof(uint32_t));
            *s = map[i].state;
         }
      }
   }

   bool size_changed = (!wlc_size_equals(&pending->geometry.size, &out->geometry.size) && !wlc_size_equals(&pending->geometry.size, &view->surface->size));

   if (pending->state != out->state || size_changed) {
      if (view->xdg_surface.resource) {
         uint32_t serial = wl_display_next_serial(wlc_display());
         xdg_surface_send_configure(view->xdg_surface.resource, pending->geometry.size.w, pending->geometry.size.h, &view->wl_state, serial);
         // XXX: Some clients such simple-damage from weston does not trigger the ack, force next commit.
         //      Otherwise we could go pending state here and wait for surface reply.
         view->ack = (size_changed ? ACK_NEXT_COMMIT : ACK_NONE);
      } else if (view->shell_surface.resource) {
         wl_shell_surface_send_configure(view->shell_surface.resource, view->resizing, pending->geometry.size.w, pending->geometry.size.h);
         view->ack = (size_changed ? ACK_NEXT_COMMIT : ACK_NONE);
      }
   }

   if (view->x11_window) {
      if (!wlc_origin_equals(&pending->geometry.origin, &out->geometry.origin))
         wlc_x11_window_position(view->x11_window, pending->geometry.origin.x, pending->geometry.origin.y);

      if (size_changed) {
         wlc_x11_window_resize(view->x11_window, pending->geometry.size.w, pending->geometry.size.h);
         view->ack = ACK_NEXT_COMMIT;
      }
   }

   if (view->ack == ACK_NONE) {
      // Commit immediately if no ack requested
      // XXX: We may need to detect frozen client
      memcpy(out, pending, sizeof(struct wlc_view_state));
   }
}

void
wlc_view_ack_surface_attach(struct wlc_view *view, struct wlc_size *old_surface_size)
{
   assert(view && old_surface_size);

   if (!view->resizing && !wlc_size_equals(&view->surface->size, old_surface_size) && !wlc_size_equals(&view->pending.geometry.size, &view->surface->size)) {
      struct wlc_geometry r = { view->pending.geometry.origin, view->surface->size };
      wlc_view_request_geometry(view, &r);
   }

   if (view->x11_window)
      view->surface->pending.opaque.extents = (pixman_box32_t){ 0, 0, view->surface->size.w, view->surface->size.h };

   if (view->ack != ACK_NEXT_COMMIT)
      return;

   bool reconfigure = false;

   if (view->resizing) {
      struct wlc_geometry r = view->pending.geometry;
      if (view->resizing & WL_SHELL_SURFACE_RESIZE_LEFT)
         r.origin.x += old_surface_size->w - view->surface->size.w;
      if (view->resizing & WL_SHELL_SURFACE_RESIZE_TOP)
         r.origin.y += old_surface_size->h - view->surface->size.h;

      // reset to before resize
      memcpy(&view->pending.geometry, &view->commit.geometry, sizeof(view->pending.geometry));

      // request resize
      if (!wlc_view_request_geometry(view, &r))
         reconfigure = true;
   }

   if (reconfigure) {
      if (view->xdg_surface.resource) {
         uint32_t serial = wl_display_next_serial(wlc_display());
         xdg_surface_send_configure(view->xdg_surface.resource, view->pending.geometry.size.w, view->pending.geometry.size.h, &view->wl_state, serial);
         view->ack = ACK_NEXT_COMMIT;
      } else if (view->shell_surface.resource) {
         wl_shell_surface_send_configure(view->shell_surface.resource, view->resizing, view->pending.geometry.size.w, view->pending.geometry.size.h);
         view->ack = ACK_NEXT_COMMIT;
      } else if (view->x11_window) {
         wlc_x11_window_resize(view->x11_window, view->pending.geometry.size.w, view->pending.geometry.size.h);
         view->ack = ACK_NEXT_COMMIT;
      }
   } else {
      memcpy(&view->commit, &view->pending, sizeof(view->commit));
      view->ack = ACK_NONE;
   }
}

void
wlc_view_get_bounds(struct wlc_view *view, struct wlc_geometry *out_bounds, struct wlc_geometry *out_visible)
{
   assert(view && out_bounds);
   memcpy(out_bounds, &view->commit.geometry, sizeof(struct wlc_geometry));

   for (struct wlc_view *parent = view->parent; !(view->type & WLC_BIT_OVERRIDE_REDIRECT) && parent; parent = parent->parent) {
      out_bounds->origin.x += parent->commit.geometry.origin.x;
      out_bounds->origin.y += parent->commit.geometry.origin.y;
   }

   if (view->xdg_surface.resource && view->commit.visible.size.w > 0 && view->commit.visible.size.h > 0) {
      // xdg-surface client that draws drop shadows or other stuff.
      // Only obey visible hints when not maximized or fullscreen.
      if (!(view->commit.state & WLC_BIT_MAXIMIZED) && !(view->commit.state & WLC_BIT_FULLSCREEN)) {
         out_bounds->origin.x -= view->commit.visible.origin.x;
         out_bounds->origin.y -= view->commit.visible.origin.y;

         // Make sure size is at least what we want, but may be bigger (shadows etc...)
         out_bounds->size.w = MAX(view->surface->size.w, view->commit.geometry.size.w);
         out_bounds->size.h = MAX(view->surface->size.h, view->commit.geometry.size.h);
      }
   }

   // Make sure bounds is never 0x0 w/h
   out_bounds->size.w = MAX(out_bounds->size.w, 1);
   out_bounds->size.h = MAX(out_bounds->size.h, 1);

   if (!out_visible)
      return;

   // Actual visible area of the view
   // The idea is to draw black borders to the bounds area, while centering the visible area.
   if ((view->x11_window || view->shell_surface.resource) && !wlc_size_equals(&view->surface->size, &out_bounds->size)) {
      out_visible->size = view->surface->size;

      // Scale visible area retaining aspect
      float ba = (float)out_bounds->size.w / (float)out_bounds->size.h;
      float sa = (float)view->surface->size.w / (float)view->surface->size.h;
      if (ba < sa) {
         out_visible->size.w *= (float)out_bounds->size.w / view->surface->size.w;
         out_visible->size.h *= (float)out_bounds->size.w / view->surface->size.w;
      } else {
         out_visible->size.w *= (float)out_bounds->size.h / view->surface->size.h;
         out_visible->size.h *= (float)out_bounds->size.h / view->surface->size.h;
      }

      // Center visible area
      out_visible->origin.x = out_bounds->origin.x + out_bounds->size.w * 0.5 - out_visible->size.w * 0.5;
      out_visible->origin.y = out_bounds->origin.y + out_bounds->size.h * 0.5 - out_visible->size.h * 0.5;

      // Make sure visible is never 0x0 w/h
      out_visible->size.w = MAX(out_visible->size.w, 1);
      out_visible->size.h = MAX(out_visible->size.h, 1);
   } else {
      // For non wl_shell or x11 surfaces, just memcpy
      memcpy(out_visible, out_bounds, sizeof(struct wlc_geometry));
   }
}

bool
wlc_view_request_geometry(struct wlc_view *view, const struct wlc_geometry *r)
{
   bool granted = true;

   if (wlc_interface()->view.request.geometry) {
      WLC_INTERFACE_EMIT(view.request.geometry, view->compositor, view, r);

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
   if (!view->created || (bool)(view->pending.state & state) == toggle)
      return;

   WLC_INTERFACE_EMIT(view.request.state, view->compositor, view, state, toggle);
}

struct wlc_space*
wlc_view_get_mapped_space(struct wlc_view *view)
{
   assert(view);
   return (view->space ? view->space : (view->compositor->output ? view->compositor->output->space : NULL));
}

void
wlc_view_defocus(struct wlc_view *view)
{
   // XXX: needed for X11 for now, rethink.
   view->compositor->seat->notify.view_unfocus(view->compositor->seat, view);
}

void
wlc_view_free(struct wlc_view *view)
{
   assert(view);

   if (view->created)
      WLC_INTERFACE_EMIT(view.destroyed, view->compositor, view);

   // XXX: ugly
   view->compositor->seat->notify.view_unfocus(view->compositor->seat, view);

   wlc_view_set_parent(view, NULL);

   struct wlc_view *v, *vn;
   wl_list_for_each_safe(v, vn, &view->childs, parent_link)
      wlc_view_set_parent(v, NULL);

   wlc_shell_surface_release(&view->shell_surface);
   wlc_xdg_surface_release(&view->xdg_surface);
   wlc_xdg_popup_release(&view->xdg_popup);

   if (view->x11_window)
      wlc_x11_window_free(view->x11_window);

   if (view->surface)
      view->surface->view = NULL;

   if (view->space)
      wl_list_remove(&view->link);

   wl_array_release(&view->wl_state);
   free(view);
}

struct wlc_view*
wlc_view_new(struct wlc_compositor *compositor, struct wlc_client *client, struct wlc_surface *surface)
{
   assert(surface && client && surface);

   struct wlc_view *view;
   if (!(view = calloc(1, sizeof(struct wlc_view))))
      return NULL;

   view->client = client;
   view->surface = surface;
   view->compositor = compositor;
   wl_array_init(&view->wl_state);
   wl_list_init(&view->childs);
   return view;
}

WLC_API uint32_t
wlc_view_get_type(struct wlc_view *view)
{
   assert(view);
   return view->type;
}

WLC_API uint32_t
wlc_view_get_state(struct wlc_view *view)
{
   assert(view);
   return view->pending.state;
}

WLC_API void
wlc_view_set_state(struct wlc_view *view, enum wlc_view_state_bit state, bool toggle)
{
   assert(view);

   if (view->x11_window)
      wlc_x11_window_set_state(view->x11_window, state, toggle);

#define BIT_TOGGLE(w, m, f) (w & ~m) | (-f & m)
   view->pending.state = BIT_TOGGLE(view->pending.state, state, toggle);
#undef BIT_TOGGLE
   update(view);
}

WLC_API const struct wlc_geometry*
wlc_view_get_geometry(struct wlc_view *view)
{
   assert(view);
   return &view->pending.geometry;
}

WLC_API void
wlc_view_set_geometry(struct wlc_view *view, const struct wlc_geometry *geometry)
{
   assert(view && geometry);
   view->pending.geometry = *geometry;
   update(view);
}

WLC_API void
wlc_view_close(struct wlc_view *view)
{
   assert(view);

   if (view->xdg_popup.resource) {
      xdg_popup_send_popup_done(view->xdg_popup.resource, wl_display_next_serial(wlc_display()));
   } else if (view->xdg_surface.resource) {
      xdg_surface_send_close(view->xdg_surface.resource);
   } else if (view->x11_window) {
      wlc_x11_window_close(view->x11_window);
   } else if (view->shell_surface.resource) {
      wlc_shell_surface_release(&view->shell_surface);
      wlc_client_free(view->client);
   }
}

WLC_API struct wl_list*
wlc_view_get_user_link(struct wlc_view *view)
{
   assert(view);
   return &view->user_link;
}

WLC_API struct wlc_view*
wlc_view_from_user_link(struct wl_list *view_link)
{
   assert(view_link);
   struct wlc_view *view;
   return wl_container_of(view_link, view, user_link);
}

WLC_API struct wl_list*
wlc_view_get_link(struct wlc_view *view)
{
   assert(view);
   return &view->link;
}

WLC_API struct wlc_view*
wlc_view_from_link(struct wl_list *view_link)
{
   assert(view_link);
   struct wlc_view *view;
   return wl_container_of(view_link, view, link);
}

WLC_API void
wlc_view_send_below(struct wlc_view *view, struct wlc_view *below)
{
   assert(view && below && view != below);

   if (below->link.next == &view->link)
      return;

   wl_list_remove(&view->link);
   wl_list_insert(&below->link, &view->link);
   update(view);
}

WLC_API void
wlc_view_send_to_back(struct wlc_view *view)
{
   assert(view);

   struct wl_list *views = &view->space->views;
   if (&view->link == views->prev)
      return;

   wl_list_remove(&view->link);
   wl_list_insert(views->prev, &view->link);
   update(view);
}

WLC_API void
wlc_view_bring_above(struct wlc_view *view, struct wlc_view *above)
{
   assert(view && above && view != above);

   if (above->link.prev == &view->link)
      return;

   wl_list_remove(&view->link);
   wl_list_insert(above->link.prev, &view->link);
   update(view);
}

WLC_API void
wlc_view_bring_to_front(struct wlc_view *view)
{
   assert(view);

   struct wl_list *views = &view->space->views;
   if (&view->link == views->prev)
      return;

   wl_list_remove(&view->link);
   wl_list_insert(views->prev, &view->link);
   update(view);
}

WLC_API void
wlc_view_set_space(struct wlc_view *view, struct wlc_space *space)
{
   assert(view);

   if (view->space == space)
      return;

   if (view->space)
      wl_list_remove(&view->link);

   if (space)
      wl_list_insert(space->views.prev, &view->link);

   if (!space || space->output != view->surface->output)
      wlc_surface_invalidate(view->surface);

   struct wlc_space *old_space = view->space;
   view->space = space;

   if (space && !view->created) {
      if (!wlc_size_equals(&view->pending.geometry.size, &wlc_size_zero))
         view->pending.geometry.size = view->surface->size;

      if (WLC_INTERFACE_EMIT_EXCEPT(view.created, false, view->compositor, view, space)) {
         wlc_view_free(view);
         return;
      }

      wlc_log(WLC_LOG_INFO, "-> Created view (%p) with surface (%p) [%ux%u]",
            view, view->surface, view->surface->size.w, view->surface->size.h);

      view->created = true;
   } else if (old_space && space) {
      wlc_surface_attach_to_output(view->surface, space->output, view->surface->commit.buffer);
      WLC_INTERFACE_EMIT(view.switch_space, view->compositor, view, old_space, space);
   }

   update(view);
}

WLC_API struct wlc_space*
wlc_view_get_space(struct wlc_view *view)
{
   assert(view);
   return view->space;
}

WLC_API void
wlc_view_set_userdata(struct wlc_view *view, void *userdata)
{
   assert(view);
   view->userdata = userdata;
}

WLC_API void*
wlc_view_get_userdata(struct wlc_view *view)
{
   assert(view);
   return view->userdata;
}

WLC_API void
wlc_view_set_title(struct wlc_view *view, const char *title)
{
   assert(view);
   wlc_string_set(&view->shell_surface.title, title, true);
}

WLC_API const char*
wlc_view_get_title(struct wlc_view *view)
{
   assert(view);
   return view->shell_surface.title.data;
}

WLC_API void
wlc_view_set_class(struct wlc_view *view, const char *_class)
{
   assert(view);
   wlc_string_set(&view->shell_surface._class, _class, true);
}

WLC_API const char*
wlc_view_get_class(struct wlc_view *view)
{
   assert(view);
   return view->shell_surface._class.data;
}

WLC_API void
wlc_view_set_parent(struct wlc_view *view, struct wlc_view *parent)
{
   assert(view && view != parent);

   if (view->parent)
      wl_list_remove(&view->parent_link);

   if ((view->parent = parent))
      wl_list_insert(&parent->childs, &view->parent_link);

   update(view);
}

WLC_API struct wlc_view*
wlc_view_get_parent(struct wlc_view *view)
{
   assert(view);
   return view->parent;
}
