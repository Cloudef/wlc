#include <stdlib.h>
#include <assert.h>
#include <wayland-server.h>
#include "internal.h"
#include "macros.h"
#include "xdg-shell.h"
#include "compositor/compositor.h"
#include "compositor/output.h"
#include "compositor/view.h"
#include "resources/types/xdg-surface.h"

static_assert_x(XDG_SHELL_VERSION_CURRENT == 5, generated_protocol_and_implementation_version_are_different);

static void
xdg_cb_shell_use_unstable_version(struct wl_client *client, struct wl_resource *resource, int32_t version)
{
   (void)client;
   if (version != XDG_SHELL_VERSION_CURRENT) {
      wl_resource_post_error(resource, 1, "xdg-shell :: unsupported version %u, supported %u", version, XDG_SHELL_VERSION_CURRENT);
      return;
   }
}

static void
xdg_cb_shell_get_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource)
{
   struct wlc_surface *surface;
   struct wlc_xdg_shell *xdg_shell;
   if (!(xdg_shell = wl_resource_get_user_data(resource)) || !(surface = convert_from_wl_resource(surface_resource, "surface")))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&xdg_shell->surfaces, client, &xdg_surface_interface, wl_resource_get_version(resource), 1, id)))
      return;

   wlc_resource_implement(r, wlc_xdg_surface_implementation(), NULL);

   struct wlc_surface_event ev = { .attach = { .type = WLC_XDG_SURFACE, .role = r }, .surface = surface, .type = WLC_SURFACE_EVENT_REQUEST_VIEW_ATTACH };
   wl_signal_emit(&wlc_system_signals()->surface, &ev);
}

static const struct xdg_popup_interface xdg_popup_implementation = {
   .destroy = wlc_cb_resource_destructor,
};

static void
xdg_cb_shell_get_popup(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *parent_resource, struct wl_resource *seat_resource, uint32_t serial, int32_t x, int32_t y)
{
   (void)seat_resource, (void)serial;

   struct wlc_surface *surface, *psurface;
   struct wlc_xdg_shell *xdg_shell;
   if (!(xdg_shell = wl_resource_get_user_data(resource)) || !(surface = convert_from_wl_resource(surface_resource, "surface")) || !(psurface = convert_from_wl_resource(parent_resource, "surface")))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&xdg_shell->popups, client, &xdg_popup_interface, wl_resource_get_version(resource), 1, id)))
      return;

   wlc_resource_implement(r, &xdg_popup_implementation, NULL);

   struct wlc_surface_event ev = { .popup = { .parent = psurface, .origin = { x, y }, .resource = r }, .surface = surface, .type = WLC_SURFACE_EVENT_REQUEST_VIEW_POPUP };
   wl_signal_emit(&wlc_system_signals()->surface, &ev);
}

static void
xdg_cb_shell_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial)
{
   (void)client, (void)resource, (void)serial;
   STUBL(resource);
}

static void
xdg_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   (void)client, (void)resource;
}

static const struct xdg_shell_interface xdg_shell_implementation = {
   .use_unstable_version = xdg_cb_shell_use_unstable_version,
   .get_xdg_surface = xdg_cb_shell_get_surface,
   .get_xdg_popup = xdg_cb_shell_get_popup,
   .pong = xdg_cb_shell_pong,
   .destroy = xdg_cb_destroy,
};

static void
xdg_shell_bind(struct wl_client *client, void *data, unsigned int version, unsigned int id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create_checked(client, &xdg_shell_interface, version, 1, id)))
      return;

   wl_resource_set_implementation(resource, &xdg_shell_implementation, data, NULL);
}

void
wlc_xdg_shell_release(struct wlc_xdg_shell *xdg_shell)
{
   if (!xdg_shell)
      return;

   if (xdg_shell->wl.xdg_shell)
      wl_global_destroy(xdg_shell->wl.xdg_shell);

   wlc_source_release(&xdg_shell->surfaces);
   wlc_source_release(&xdg_shell->popups);
   memset(xdg_shell, 0, sizeof(struct wlc_xdg_shell));
}

bool
wlc_xdg_shell(struct wlc_xdg_shell *xdg_shell)
{
   assert(xdg_shell);
   memset(xdg_shell, 0, sizeof(struct wlc_xdg_shell));

   if (!(xdg_shell->wl.xdg_shell = wl_global_create(wlc_display(), &xdg_shell_interface, 1, xdg_shell, xdg_shell_bind)))
      goto xdg_shell_interface_fail;

   if (!wlc_source(&xdg_shell->surfaces, "xdg-surface", NULL, NULL, 32, sizeof(struct wlc_resource)) ||
       !wlc_source(&xdg_shell->popups, "xdg-popup", NULL, NULL, 4, sizeof(struct wlc_resource)))
      goto fail;

   return xdg_shell;

xdg_shell_interface_fail:
   wlc_log(WLC_LOG_WARN, "Failed to bind xdg_shell interface");
fail:
   wlc_xdg_shell_release(xdg_shell);
   return NULL;
}
