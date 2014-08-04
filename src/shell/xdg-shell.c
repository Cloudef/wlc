#include "xdg-shell.h"
#include "xdg-surface.h"
#include "macros.h"

#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "compositor/view.h"

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <wayland-server.h>
#include "wayland-xdg-shell-server-protocol.h"

static void
xdg_cb_shell_use_unstable_version(struct wl_client *client, struct wl_resource *resource, int32_t version)
{
   (void)client;
   if (version > XDG_SHELL_VERSION_CURRENT) {
      wl_resource_post_error(resource, 1, "xdg-shell :: version not implemented yet.");
      return;
   }
}

static void
xdg_cb_shell_get_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource)
{
   (void)resource;

   struct wlc_surface *surface = wl_resource_get_user_data(surface_resource);

   struct wlc_view *view;
   if (!(view = wlc_view_for_surface_in_list(surface, &surface->compositor->views))) {
      wl_resource_post_error(resource, 1, "view was not found for client");
      return;
   }

   struct wl_resource *xdg_surface_resource;
   if (!(xdg_surface_resource = wl_resource_create(client, &xdg_surface_interface, 1, id))) {
      wl_resource_post_no_memory(resource);
      return;
   }

   struct wlc_xdg_surface *xdg_surface;
   if (!(xdg_surface = wlc_xdg_surface_new(surface))) {
      wl_resource_destroy(xdg_surface_resource);
      wl_resource_post_no_memory(resource);
      return;
   }

   wlc_view_set_xdg_surface(view, xdg_surface);
   wlc_xdg_surface_implement(xdg_surface, xdg_surface_resource);
}

static void
xdg_cb_shell_get_popup(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *parent, struct wl_resource *seat, uint32_t serial, int32_t x, int32_t y, uint32_t flags)
{
   (void)client, (void)resource, (void)id, (void)parent, (void)seat, (void)serial, (void)x, (void)y, (void)flags;
   STUB(surface_resource);
}


static void
xdg_cb_shell_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial)
{
   (void)client, (void)serial;
   STUB(resource);
}

static const struct xdg_shell_interface xdg_shell_implementation = {
   xdg_cb_shell_use_unstable_version,
   xdg_cb_shell_get_surface,
   xdg_cb_shell_get_popup,
   xdg_cb_shell_pong
};

static void
xdg_shell_bind(struct wl_client *client, void *data, unsigned int version, unsigned int id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create(client, &xdg_shell_interface, MIN(version, 1), id))) {
      wl_client_post_no_memory(client);
      fprintf(stderr, "-!- failed create resource or bad version (%u > %u)", version, 1);
      return;
   }

   wl_resource_set_implementation(resource, &xdg_shell_implementation, data, NULL);
}

void
wlc_xdg_shell_free(struct wlc_xdg_shell *xdg_shell)
{
   assert(xdg_shell);

   if (xdg_shell->global)
      wl_global_destroy(xdg_shell->global);

   free(xdg_shell);
}

struct wlc_xdg_shell*
wlc_xdg_shell_new(struct wlc_compositor *compositor)
{
   struct wlc_xdg_shell *xdg_shell;
   if (!(xdg_shell = calloc(1, sizeof(struct wlc_xdg_shell))))
      goto out_of_memory;

   if (!(xdg_shell->global = wl_global_create(compositor->display, &xdg_shell_interface, 1, xdg_shell, xdg_shell_bind)))
      goto xdg_shell_interface_fail;

   xdg_shell->compositor = compositor;
   return xdg_shell;

out_of_memory:
   fprintf(stderr, "-!- out of memory\n");
   goto fail;
xdg_shell_interface_fail:
   fprintf(stderr, "-!- failed to bind xdg_shell interface\n");
fail:
   if (xdg_shell)
      wlc_xdg_shell_free(xdg_shell);
   return NULL;
}
