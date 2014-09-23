#include "view.h"
#include "visibility.h"
#include "compositor.h"
#include "surface.h"

#include "shell/surface.h"
#include "shell/xdg-surface.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <wayland-server.h>
#include "wayland-xdg-shell-server-protocol.h"

#define BIT_TOGGLE(w, m, f) (w & ~m) | (-f & m)
#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

static void
update_state(struct wlc_view *view)
{
   assert(view);

   if (!view->xdg_surface)
      return;

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

   struct wl_array array;
   wl_array_init(&array);

   for (unsigned int i = 0; map[i].state != 0; ++i) {
      if (view->state & map[i].bit) {
         uint32_t *s = wl_array_add(&array, sizeof(uint32_t));
         *s = map[i].state;
      }
   }

   uint32_t serial = wl_display_next_serial(view->surface->compositor->display);
   xdg_surface_send_configure(view->xdg_surface->shell_surface->resource, view->geometry.w, view->geometry.h, &array, serial);
   wl_array_copy(&view->stored_state, &array);
}

void
wlc_view_get_bounds(struct wlc_view *view, struct wlc_geometry *out_bounds)
{
   assert(out_bounds);
   memcpy(out_bounds, &view->geometry, sizeof(struct wlc_geometry));

   if (view->xdg_surface && view->xdg_surface->visible_geometry.w > 0 && view->xdg_surface->visible_geometry.h > 0) {
      out_bounds->x -= view->xdg_surface->visible_geometry.x;
      out_bounds->y -= view->xdg_surface->visible_geometry.y;
      out_bounds->w -= out_bounds->w - view->xdg_surface->visible_geometry.w;
      out_bounds->w += view->xdg_surface->visible_geometry.y * 2;
      out_bounds->h -= out_bounds->h - view->xdg_surface->visible_geometry.h;
      out_bounds->h += view->xdg_surface->visible_geometry.x * 2;

      if ((view->state & WLC_BIT_MAXIMIZED) || (view->state & WLC_BIT_FULLSCREEN)) {
         out_bounds->w = MIN(out_bounds->w, view->geometry.w);
         out_bounds->h = MIN(out_bounds->h, view->geometry.h);
      }
   }

   /* make sure bounds is never 0x0 w/h */
   out_bounds->w = MAX(out_bounds->w, 1);
   out_bounds->h = MAX(out_bounds->h, 1);
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

struct wlc_view*
wlc_view_for_surface_id_in_list(uint32_t surface_id, struct wl_list *list)
{
   assert(list);

   struct wlc_view *view;
   wl_list_for_each(view, list, link) {
      if (wl_resource_get_id(view->surface->resource) == surface_id)
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
   wl_list_remove(&view->link);
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
   wl_array_init(&view->stored_state);
   return view;
}

WLC_API void
wlc_view_set_maximized(struct wlc_view *view, bool maximized)
{
   assert(view);

   if (maximized == (view->state & WLC_BIT_MAXIMIZED))
      return;

   view->state = BIT_TOGGLE(view->state, WLC_BIT_MAXIMIZED, maximized);
   update_state(view);
}

WLC_API void
wlc_view_set_fullscreen(struct wlc_view *view, bool fullscreen)
{
   assert(view);

   if (fullscreen == (view->state & WLC_BIT_FULLSCREEN))
      return;

   view->state = BIT_TOGGLE(view->state, WLC_BIT_FULLSCREEN, fullscreen);
   update_state(view);
}

WLC_API void
wlc_view_set_resizing(struct wlc_view *view, bool resizing)
{
   assert(view);

   if (resizing == (view->state & WLC_BIT_RESIZING))
      return;

   view->state = BIT_TOGGLE(view->state, WLC_BIT_RESIZING, resizing);
   update_state(view);
}

WLC_API void
wlc_view_set_active(struct wlc_view *view, bool active)
{
   assert(view);

   if (active == (view->state & WLC_BIT_ACTIVATED))
      return;

   view->state = BIT_TOGGLE(view->state, WLC_BIT_ACTIVATED, active);
   update_state(view);
}

WLC_API void
wlc_view_resize(struct wlc_view *view, uint32_t width, uint32_t height)
{
   assert(view);

   if (view->xdg_surface) {
      uint32_t serial = wl_display_next_serial(view->surface->compositor->display);
      xdg_surface_send_configure(view->xdg_surface->shell_surface->resource, width, height, &view->stored_state, serial);
   }

   view->geometry.w = width;
   view->geometry.h = height;
}

WLC_API void
wlc_view_position(struct wlc_view *view, int32_t x, int32_t y)
{
   assert(view);
   view->geometry.x = x;
   view->geometry.y = y;
}

WLC_API void
wlc_view_close(struct wlc_view *view)
{
   assert(view);

   if (!view->xdg_surface)
      return;

   xdg_surface_send_close(view->xdg_surface->shell_surface->resource);
}

WLC_API struct wl_list*
wlc_view_get_link(struct wlc_view *view)
{
   assert(view);
   return &view->user_link;
}

WLC_API struct wlc_view*
wlc_view_from_link(struct wl_list *view_link)
{
   assert(view_link);
   struct wlc_view *view;
   return wl_container_of(view_link, view, user_link);
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

   struct wl_list *views = &view->surface->compositor->views;
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

   struct wl_list *views = &view->surface->compositor->views;
   if (&view->link == views->prev)
      return;

   wl_list_remove(&view->link);
   wl_list_insert(views->prev, &view->link);
}
