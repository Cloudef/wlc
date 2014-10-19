#include "wlc.h"
#include "macros.h"
#include "xdg-shell.h"
#include "xdg-surface.h"

#include "compositor/compositor.h"
#include "compositor/surface.h"
#include "compositor/output.h"
#include "compositor/view.h"

#include "seat/client.h"

#include <stdlib.h>
#include <assert.h>

#include <wayland-server.h>
#include "wayland-xdg-shell-server-protocol.h"

static void
xdg_cb_shell_use_unstable_version(struct wl_client *wl_client, struct wl_resource *resource, int32_t version)
{
   (void)wl_client;
   if (version > XDG_SHELL_VERSION_CURRENT) {
      wl_resource_post_error(resource, 1, "xdg-shell :: version not implemented yet.");
      return;
   }
}

static void
xdg_cb_shell_get_surface(struct wl_client *wl_client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource)
{
   struct wlc_xdg_shell *xdg_shell = wl_resource_get_user_data(resource);
   struct wlc_surface *surface = wl_resource_get_user_data(surface_resource);

   struct wlc_client *client;
   if (!(client = wlc_client_for_client_with_wl_client_in_list(wl_client, &xdg_shell->compositor->clients))) {
      wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT, "Could not find wlc_client for wl_client");
      return;
   }

   struct wl_resource *xdg_surface_resource;
   if (!(xdg_surface_resource = wl_resource_create(wl_client, &xdg_surface_interface, wl_resource_get_version(resource), id))) {
      wl_resource_post_no_memory(resource);
      return;
   }

   if (!surface->view && !(surface->view = wlc_view_new(xdg_shell->compositor, client, surface))) {
      wl_resource_destroy(xdg_surface_resource);
      wl_resource_post_no_memory(resource);
      return;
   }

   wlc_xdg_surface_implement(&surface->view->xdg_surface, surface->view, xdg_surface_resource);
}

static void
xdg_cb_shell_get_popup(struct wl_client *wl_client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *parent, struct wl_resource *seat, uint32_t serial, int32_t x, int32_t y, uint32_t flags)
{
   (void)wl_client, (void)id, (void)parent, (void)seat, (void)serial, (void)flags;
   struct wlc_xdg_shell *xdg_shell = wl_resource_get_user_data(resource);
   struct wlc_surface *surface = wl_resource_get_user_data(surface_resource);

   struct wlc_client *client;
   if (!(client = wlc_client_for_client_with_wl_client_in_list(wl_client, &xdg_shell->compositor->clients))) {
      wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT, "Could not find wlc_client for wl_client");
      return;
   }

   struct wl_resource *xdg_popup_resource;
   if (!(xdg_popup_resource = wl_resource_create(wl_client, &xdg_popup_interface, wl_resource_get_version(resource), id))) {
      wl_resource_post_no_memory(resource);
      return;
   }

   if (!surface->view && !(surface->view = wlc_view_new(xdg_shell->compositor, client, surface))) {
      wl_resource_destroy(xdg_popup_resource);
      wl_resource_post_no_memory(resource);
      return;
   }

   wlc_view_position(surface->view, x, y);
   wlc_xdg_popup_implement(&surface->view->xdg_popup, surface->view, xdg_popup_resource);
   surface->view->type |= WLC_BIT_POPUP;
}

static void
xdg_cb_shell_pong(struct wl_client *wl_client, struct wl_resource *resource, uint32_t serial)
{
   (void)wl_client, (void)serial;
   STUB(resource);
}

static const struct xdg_shell_interface xdg_shell_implementation = {
   .use_unstable_version = xdg_cb_shell_use_unstable_version,
   .get_xdg_surface = xdg_cb_shell_get_surface,
   .get_xdg_popup = xdg_cb_shell_get_popup,
   .pong = xdg_cb_shell_pong
};

static void
xdg_shell_bind(struct wl_client *wl_client, void *data, unsigned int version, unsigned int id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create(wl_client, &xdg_shell_interface, MIN(version, 1), id))) {
      wl_client_post_no_memory(wl_client);
      wlc_log(WLC_LOG_WARN, "Failed create resource or bad version (%u > %u)", version, 1);
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
   wlc_log(WLC_LOG_WARN, "Out of memory");
   goto fail;
xdg_shell_interface_fail:
   wlc_log(WLC_LOG_WARN, "Failed to bind xdg_shell interface");
fail:
   if (xdg_shell)
      wlc_xdg_shell_free(xdg_shell);
   return NULL;
}
