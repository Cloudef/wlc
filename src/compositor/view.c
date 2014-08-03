#include "view.h"
#include "surface.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <wayland-server.h>

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
   return view;
}
