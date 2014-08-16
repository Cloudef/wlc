#include "view.h"
#include "visibility.h"
#include "compositor.h"
#include "surface.h"

#include "shell/surface.h"
#include "shell/xdg-surface.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <wayland-server.h>
#include "wayland-xdg-shell-server-protocol.h"

#define BIT_TOGGLE(w, m, f) (w & ~m) | (-f & m)

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
   xdg_surface_send_configure(view->xdg_surface->shell_surface->resource, view->surface->width, view->surface->height, &array, serial);
   wl_array_copy(&view->stored_state, &array);
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

   if (!view->xdg_surface)
      return;

   uint32_t serial = wl_display_next_serial(view->surface->compositor->display);
   xdg_surface_send_configure(view->xdg_surface->shell_surface->resource, width, height, &view->stored_state, serial);

   view->surface->width = width;
   view->surface->height = height;
}

WLC_API void
wlc_view_position(struct wlc_view *view, int32_t x, int32_t y)
{
   assert(view);

   int32_t vx = 0, vy = 0;
#if 0
   if (view->xdg_surface) {
      vx = view->xdg_surface->visible_geometry.x;
      vy = view->xdg_surface->visible_geometry.y;
   }
#endif

   view->x = x - vx;
   view->y = y - vy;
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
