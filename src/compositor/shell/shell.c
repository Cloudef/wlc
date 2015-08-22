#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <wayland-server.h>
#include "internal.h"
#include "macros.h"
#include "shell.h"
#include "resources/resources.h"
#include "resources/types/shell-surface.h"

static void
wl_cb_shell_get_shell_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource)
{
   struct wlc_shell *shell;
   struct wlc_surface *surface;
   if (!(shell = wl_resource_get_user_data(resource)) || !(surface = convert_from_wl_resource(surface_resource, "surface")))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&shell->surfaces, client, &wl_shell_surface_interface, wl_resource_get_version(resource), 1, id)))
      return;

   wlc_resource_implement(r, wlc_shell_surface_implementation(), NULL);

   struct wlc_surface_event ev = { .attach = { .type = WLC_SHELL_SURFACE, .shell_surface = r }, .surface = surface, .type = WLC_SURFACE_EVENT_REQUEST_VIEW_ATTACH };
   wl_signal_emit(&wlc_system_signals()->surface, &ev);
}

static const struct wl_shell_interface wl_shell_implementation = {
   .get_shell_surface = wl_cb_shell_get_shell_surface
};

static void
wl_shell_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create_checked(client, &wl_shell_interface, version, 1, id)))
      return;

   wl_resource_set_implementation(resource, &wl_shell_implementation, data, NULL);
}

void
wlc_shell_release(struct wlc_shell *shell)
{
   if (!shell)
      return;

   if (shell->wl.shell)
      wl_global_destroy(shell->wl.shell);

   wlc_source_release(&shell->surfaces);
   memset(shell, 0, sizeof(struct wlc_shell));
}

bool
wlc_shell(struct wlc_shell *shell)
{
   assert(shell);
   memset(shell, 0, sizeof(struct wlc_shell));

   if (!(shell->wl.shell = wl_global_create(wlc_display(), &wl_shell_interface, 1, shell, wl_shell_bind)))
      goto shell_interface_fail;

   if (!wlc_source(&shell->surfaces, "shell-surface", NULL, NULL, 32, sizeof(struct wlc_resource)))
      goto fail;

   return true;

shell_interface_fail:
   wlc_log(WLC_LOG_WARN, "Failed to bind shell interface");
fail:
   wlc_shell_release(shell);
   return false;
}
