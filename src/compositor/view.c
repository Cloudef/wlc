#include "view.h"
#include "macros.h"
#include "visibility.h"
#include "compositor.h"
#include "output.h"
#include "surface.h"

#include "shell/surface.h"
#include "shell/xdg-surface.h"

#include "seat/seat.h"

#include "xwayland/xwm.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <wayland-server.h>
#include "wayland-xdg-shell-server-protocol.h"

void
wlc_view_commit_state(struct wlc_view *view, struct wlc_view_state *pending, struct wlc_view_state *out)
{
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

   uint32_t serial = wl_display_next_serial(view->compositor->display);
   if (pending->state != out->state || !wlc_size_equals(&pending->geometry.size, &out->geometry.size)) {
      if (view->xdg_surface.resource)
         xdg_surface_send_configure(view->xdg_surface.resource, pending->geometry.size.w, pending->geometry.size.h, &view->wl_state, serial);

      if (view->x11_window && !wlc_size_equals(&pending->geometry.size, &out->geometry.size))
         wlc_x11_window_resize(view->x11_window, pending->geometry.size.w, pending->geometry.size.h);
   }

   if (view->x11_window && !wlc_origin_equals(&pending->geometry.origin, &out->geometry.origin))
      wlc_x11_window_position(view->x11_window, pending->geometry.origin.x, pending->geometry.origin.y);

   memcpy(out, pending, sizeof(struct wlc_view_state));
}

void
wlc_view_get_bounds(struct wlc_view *view, struct wlc_geometry *out_bounds)
{
   assert(out_bounds);
   memcpy(out_bounds, &view->commit.geometry, sizeof(struct wlc_geometry));

   if (view->xdg_surface.resource && view->xdg_surface.visible_geometry.size.w > 0 && view->xdg_surface.visible_geometry.size.h > 0) {
      out_bounds->origin.x -= view->xdg_surface.visible_geometry.origin.x;
      out_bounds->origin.y -= view->xdg_surface.visible_geometry.origin.y;
      out_bounds->size.w -= out_bounds->size.w - view->xdg_surface.visible_geometry.size.w;
      out_bounds->size.w += view->xdg_surface.visible_geometry.origin.y * 2;
      out_bounds->size.h -= out_bounds->size.h - view->xdg_surface.visible_geometry.size.h;
      out_bounds->size.h += view->xdg_surface.visible_geometry.origin.x * 2;

      if ((view->commit.state & WLC_BIT_MAXIMIZED) || (view->commit.state & WLC_BIT_FULLSCREEN)) {
         out_bounds->size.w = MIN(out_bounds->size.w, view->commit.geometry.size.w);
         out_bounds->size.h = MIN(out_bounds->size.h, view->commit.geometry.size.h);
      }
   }

   /* make sure bounds is never 0x0 w/h */
   out_bounds->size.w = MAX(out_bounds->size.w, 1);
   out_bounds->size.h = MAX(out_bounds->size.h, 1);
}

void
wlc_view_request_state(struct wlc_view *view, enum wlc_view_bit state, bool toggle)
{
   if (!view->created || !view->compositor->interface.view.request.state)
      return;

   view->compositor->interface.view.request.state(view->compositor, view, state, toggle);
}

void
wlc_view_set_parent(struct wlc_view *view, struct wlc_view *parent)
{
   assert(view);
   view->parent = parent;
}

void
wlc_view_free(struct wlc_view *view)
{
   assert(view);

   if (view->created && view->compositor->interface.view.destroyed)
      view->compositor->interface.view.destroyed(view->compositor, view);

   view->compositor->seat->notify.view_unfocus(view->compositor->seat, view);

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
   assert(surface);

   struct wlc_view *view;
   if (!(view = calloc(1, sizeof(struct wlc_view))))
      return NULL;

   view->client = client;
   view->surface = surface;
   view->compositor = compositor;
   wl_array_init(&view->wl_state);
   return view;
}

WLC_API uint32_t
wlc_view_get_state(struct wlc_view *view)
{
   assert(view);
   return view->pending.state;
}

WLC_API void
wlc_view_set_state(struct wlc_view *view, enum wlc_view_bit state, bool toggle)
{
   assert(view);
#define BIT_TOGGLE(w, m, f) (w & ~m) | (-f & m)
   view->pending.state = BIT_TOGGLE(view->pending.state, state, toggle);
#undef BIT_TOGGLE
}

WLC_API void
wlc_view_resize(struct wlc_view *view, uint32_t width, uint32_t height)
{
   assert(view);
   view->pending.geometry.size = (struct wlc_size){ width, height };
}

WLC_API void
wlc_view_position(struct wlc_view *view, int32_t x, int32_t y)
{
   assert(view);
   view->pending.geometry.origin = (struct wlc_origin){ x, y };
}

WLC_API void
wlc_view_close(struct wlc_view *view)
{
   assert(view);

   if (view->xdg_surface.resource) {
      xdg_surface_send_close(view->xdg_surface.resource);
   } else if (view->x11_window) {
      wlc_x11_window_close(view->x11_window);
   } else {
      wlc_surface_free(view->surface);
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
   assert(view && below);

   if (below->link.next == &view->link)
      return;

   wl_list_remove(&view->link);
   wl_list_insert(below->link.next, &view->link);
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
}

WLC_API void
wlc_view_bring_above(struct wlc_view *view, struct wlc_view *above)
{
   assert(view && above);

   if (above->link.prev == &view->link)
      return;

   wl_list_remove(&view->link);
   wl_list_insert(above->link.prev, &view->link);
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
}

WLC_API void
wlc_view_set_space(struct wlc_view *view, struct wlc_space *space)
{
   assert(view);

   if (view->space == space)
      return;

   if (view->created && space && view->compositor->interface.view.will_move_to_space)
      view->compositor->interface.view.will_move_to_space(view->compositor, view, space);

   if (view->space)
      wl_list_remove(&view->link);

   if (space)
      wl_list_insert(space->views.prev, &view->link);

   if (!space || space->output != view->surface->output)
      wlc_surface_invalidate(view->surface);

   view->space = space;

   if (space && !view->created) {
      view->pending.geometry.size = view->surface->size;

      if (view->compositor->interface.view.created &&
         !view->compositor->interface.view.created(view->compositor, view, space)) {
         wlc_view_free(view);
         return;
      }

      view->created = true;
   }
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

WLC_API void
wlc_view_set_class(struct wlc_view *view, const char *_class)
{
   assert(view);
   wlc_string_set(&view->shell_surface._class, _class, true);
}
