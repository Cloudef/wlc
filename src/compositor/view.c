#include "view.h"
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

#ifndef MIN
#  define MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
#  define MAX(a,b) (((a)>(b))?(a):(b))
#endif

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

      if (view->x11_window) {
         if ((pending->state & WLC_BIT_ACTIVATED) != (out->state & WLC_BIT_ACTIVATED)) {
            wlc_x11_window_set_active(view->x11_window, (pending->state & WLC_BIT_ACTIVATED));

            if (pending->state & WLC_BIT_ACTIVATED)
               wlc_compositor_focus_view(view->surface->compositor, view);
         }
      }
   }

   uint32_t serial = wl_display_next_serial(view->surface->compositor->display);
   if (pending->state != out->state || !wlc_size_equals(&pending->geometry.size, &out->geometry.size)) {
      if (view->xdg_surface)
         xdg_surface_send_configure(view->xdg_surface->shell_surface->resource, pending->geometry.size.w, pending->geometry.size.h, &view->wl_state, serial);

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

   if (view->xdg_surface && view->xdg_surface->visible_geometry.size.w > 0 && view->xdg_surface->visible_geometry.size.h > 0) {
      out_bounds->origin.x -= view->xdg_surface->visible_geometry.origin.x;
      out_bounds->origin.y -= view->xdg_surface->visible_geometry.origin.y;
      out_bounds->size.w -= out_bounds->size.w - view->xdg_surface->visible_geometry.size.w;
      out_bounds->size.w += view->xdg_surface->visible_geometry.origin.y * 2;
      out_bounds->size.h -= out_bounds->size.h - view->xdg_surface->visible_geometry.size.h;
      out_bounds->size.h += view->xdg_surface->visible_geometry.origin.x * 2;

      if ((view->commit.state & WLC_BIT_MAXIMIZED) || (view->commit.state & WLC_BIT_FULLSCREEN)) {
         out_bounds->size.w = MIN(out_bounds->size.w, view->commit.geometry.size.w);
         out_bounds->size.h = MIN(out_bounds->size.h, view->commit.geometry.size.h);
      }
   }

   /* make sure bounds is never 0x0 w/h */
   out_bounds->size.w = MAX(out_bounds->size.w, 1);
   out_bounds->size.h = MAX(out_bounds->size.h, 1);
}

struct wlc_view*
wlc_view_for_surface_in_list(struct wlc_surface *surface, struct wl_list *list)
{
   assert(surface && list);

   struct wlc_view *view;
   wl_list_for_each(view, list, link) {
      if (view->surface == surface)
         return view;
   }

   return NULL;
}

void
wlc_view_set_xdg_surface(struct wlc_view *view, struct wlc_xdg_surface *xdg_surface)
{
   assert(view);
   view->xdg_surface = xdg_surface;
}

void
wlc_view_set_shell_surface(struct wlc_view *view, struct wlc_shell_surface *shell_surface)
{
   assert(view);
   view->shell_surface = shell_surface;
}

void
wlc_view_free(struct wlc_view *view)
{
   assert(view);

   if (view->x11_window)
      wlc_x11_window_free(view->x11_window);

   view->surface->compositor->seat->notify.view_unfocus(view->surface->compositor->seat, view);
   wl_list_remove(&view->link);
   wl_array_release(&view->wl_state);
   free(view);
}

struct wlc_view*
wlc_view_new(struct wlc_client *client, struct wlc_surface *surface)
{
   assert(surface);

   struct wlc_view *view;
   if (!(view = calloc(1, sizeof(struct wlc_view))))
      return NULL;

   view->client = client;
   view->surface = surface;
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

   if (view->xdg_surface) {
      xdg_surface_send_close(view->xdg_surface->shell_surface->resource);
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

   struct wl_list *views = &view->surface->space->views;
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

   struct wl_list *views = &view->surface->space->views;
   if (&view->link == views->prev)
      return;

   wl_list_remove(&view->link);
   wl_list_insert(views->prev, &view->link);
}

WLC_API struct wlc_space*
wlc_view_get_space(struct wlc_view *view)
{
   assert(view);
   return view->surface->space;
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
