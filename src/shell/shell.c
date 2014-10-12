#include "shell.h"
#include "surface.h"
#include "macros.h"

#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "compositor/output.h"
#include "compositor/view.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <wayland-server.h>

static void
wl_cb_shell_get_shell_surface(struct wl_client *wl_client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource)
{
   struct wlc_surface *surface = wl_resource_get_user_data(surface_resource);

   struct wlc_view *view;
   if (!(view = wlc_view_for_surface_in_list(surface, &surface->compositor->unmapped)) &&
       !(view = wlc_view_for_surface_in_list(surface, &surface->space->views))) {
      wl_resource_post_error(resource, 1, "view was not found for client");
      return;
   }

   struct wl_resource *shell_surface_resource;
   if (!(shell_surface_resource = wl_resource_create(wl_client, &wl_shell_surface_interface, 1, id))) {
      wl_resource_post_no_memory(resource);
      return;
   }

   struct wlc_shell_surface *shell_surface;
   if (!(shell_surface = wlc_shell_surface_new(surface))) {
      wl_resource_destroy(shell_surface_resource);
      wl_resource_post_no_memory(resource);
      return;
   }

   wlc_view_set_shell_surface(view, shell_surface);
   wlc_shell_surface_implement(shell_surface, shell_surface_resource);
}

static const struct wl_shell_interface wl_shell_implementation = {
   wl_cb_shell_get_shell_surface
};

static void
wl_shell_bind(struct wl_client *wl_client, void *data, unsigned int version, unsigned int id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create(wl_client, &wl_shell_interface, MIN(version, 1), id))) {
      wl_client_post_no_memory(wl_client);
      fprintf(stderr, "-!- failed create resource or bad version (%u > %u)\n", version, 1);
      return;
   }

   wl_resource_set_implementation(resource, &wl_shell_implementation, data, NULL);
}

void
wlc_shell_free(struct wlc_shell *shell)
{
   assert(shell);

   if (shell->global)
      wl_global_destroy(shell->global);

   free(shell);
}

struct wlc_shell*
wlc_shell_new(struct wlc_compositor *compositor)
{
   struct wlc_shell *shell;
   if (!(shell = calloc(1, sizeof(struct wlc_shell))))
      goto out_of_memory;

   if (!(shell->global = wl_global_create(compositor->display, &wl_shell_interface, 1, shell, wl_shell_bind)))
      goto shell_interface_fail;

   shell->compositor = compositor;
   return shell;

out_of_memory:
   fprintf(stderr, "-!- out of memory\n");
   goto fail;
shell_interface_fail:
   fprintf(stderr, "-!- failed to bind shell interface\n");
fail:
   if (shell)
      wlc_shell_free(shell);
   return NULL;
}
