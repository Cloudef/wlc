#include "view.h"
#include "compositor.h"
#include "surface.h"
#include "shell/surface.h"
#include "shell/xdg-surface.h"
#include "visibility.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <wayland-server.h>
#include "wayland-xdg-shell-server-protocol.h"

struct wl_client*
wlc_view_get_client(struct wlc_view *view)
{
   assert(view);
   return wl_resource_get_client(view->surface->resource);
}

struct wlc_view*
wlc_view_for_input_resource_in_list(struct wl_resource *resource, enum wlc_input_type type, struct wl_list *list)
{
   assert(resource && list);

   struct wlc_view *view;
   wl_list_for_each(view, list, link) {
      if (view->input[type] == resource)
         return view;
   }

   return NULL;
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
wlc_view_for_client_in_list(struct wl_client *client, struct wl_list *list)
{
   assert(client && list);

   struct wlc_view *view;
   wl_list_for_each(view, list, link) {
      if (wlc_view_get_client(view) == client)
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
wlc_view_new(struct wlc_surface *surface)
{
   assert(surface);

   struct wlc_view *view;
   if (!(view = calloc(1, sizeof(struct wlc_view))))
      return NULL;

   view->surface = surface;
   wl_array_init(&view->state);
   return view;
}

WLC_API void
wlc_view_set_state(struct wlc_view *view, const uint32_t *states, uint32_t memb)
{
   assert(view);

   if (!view->xdg_surface)
      return;

   struct wl_array array;
   wl_array_init(&array);

   for (uint32_t i = 0; i < memb; ++i) {
      uint32_t *s = wl_array_add(&array, sizeof(uint32_t));
      *s = states[i];
   }

   uint32_t serial = wl_display_next_serial(view->surface->compositor->display);
   xdg_surface_send_configure(view->xdg_surface->shell_surface->resource, view->surface->width, view->surface->height, &array, serial);
   wl_array_copy(&view->state, &array);
}

WLC_API void
wlc_view_resize(struct wlc_view *view, uint32_t width, uint32_t height)
{
   assert(view);

   if (!view->xdg_surface)
      return;

   uint32_t serial = wl_display_next_serial(view->surface->compositor->display);
   xdg_surface_send_configure(view->xdg_surface->shell_surface->resource, width, height, &view->state, serial);
}

WLC_API void
wlc_view_position(struct wlc_view *view, int32_t x, int32_t y)
{
   assert(view);

   int32_t vx = 0, vy = 0;
   if (view->xdg_surface) {
      vx = view->xdg_surface->visible_geometry.x;
      vy = view->xdg_surface->visible_geometry.y;
   }

   view->x = x - vx;
   view->y = y - vy;
}
