#include <stdlib.h>
#include <assert.h>
#include <wayland-server.h>
#include "internal.h"
#include "macros.h"
#include "xdg-shell.h"
#include "compositor/compositor.h"
#include "compositor/output.h"
#include "compositor/view.h"
#include "resources/types/xdg-toplevel.h"

struct xdg_surface {
   wlc_resource surface;
};

static struct wlc_surface*
xdg_surface_get_surface(struct xdg_surface *xdg_surface)
{
   return (xdg_surface ? convert_from_wlc_resource(xdg_surface->surface, "surface") : NULL);
}

static void
xdg_cb_popup_grab(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat, uint32_t serial)
{
   (void)client, (void)seat, (void)serial;
   STUB(resource);
}

static const struct zxdg_popup_v6_interface zxdg_popup_v6_implementation = {
   .destroy = wlc_cb_resource_destructor,
   .grab = xdg_cb_popup_grab,
};

static void
xdg_cb_surface_get_popup(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *parent, struct wl_resource *positioner)
{
   (void)positioner;

   struct wlc_xdg_shell *xdg_shell;
   struct wlc_surface *surface, *psurface;
   if (!(xdg_shell = wl_resource_get_user_data(resource)) || !(surface = xdg_surface_get_surface(convert_from_wl_resource(resource, "xdg-surface"))) || !(psurface = xdg_surface_get_surface(convert_from_wl_resource(parent, "xdg-surface"))))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&xdg_shell->popups, client, &zxdg_popup_v6_interface, wl_resource_get_version(resource), 1, id)))
      return;

   wlc_resource_implement(r, &zxdg_popup_v6_implementation, NULL);

   {
      struct wlc_surface_event ev = { .attach = { .type = WLC_XDG_SURFACE, .role = wlc_resource_from_wl_resource(resource) }, .surface = surface, .type = WLC_SURFACE_EVENT_REQUEST_VIEW_ATTACH };
      wl_signal_emit(&wlc_system_signals()->surface, &ev);
   }

   {
      struct wlc_surface_event ev = { .popup = { .parent = psurface, .role = r }, .surface = surface, .type = WLC_SURFACE_EVENT_REQUEST_VIEW_POPUP };
      wl_signal_emit(&wlc_system_signals()->surface, &ev);
   }

   zxdg_surface_v6_send_configure(resource, wl_display_next_serial(wlc_display()));
}

static void
xdg_cb_surface_get_toplevel(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   struct wlc_surface *surface;
   struct wlc_xdg_shell *xdg_shell;
   if (!(xdg_shell = wl_resource_get_user_data(resource)) || !(surface = xdg_surface_get_surface(convert_from_wl_resource(resource, "xdg-surface"))))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&xdg_shell->toplevels, client, &zxdg_toplevel_v6_interface, wl_resource_get_version(resource), 1, id)))
      return;

   wlc_resource_implement(r, wlc_xdg_toplevel_implementation(), NULL);

   {
      struct wlc_surface_event ev = { .attach = { .type = WLC_XDG_SURFACE, .role = wlc_resource_from_wl_resource(resource) }, .surface = surface, .type = WLC_SURFACE_EVENT_REQUEST_VIEW_ATTACH };
      wl_signal_emit(&wlc_system_signals()->surface, &ev);
   }

   {
      struct wlc_surface_event ev = { .attach = { .type = WLC_XDG_TOPLEVEL_SURFACE, .role = r }, .surface = surface, .type = WLC_SURFACE_EVENT_REQUEST_VIEW_ATTACH };
      wl_signal_emit(&wlc_system_signals()->surface, &ev);
   }

   zxdg_surface_v6_send_configure(resource, wl_display_next_serial(wlc_display()));
}

static void
xdg_cb_surface_ack_configure(struct wl_client *client, struct wl_resource *resource, uint32_t serial)
{
   (void)client, (void)serial, (void)resource;
}

static void
xdg_cb_surface_set_window_geometry(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y, int32_t width, int32_t height)
{
   (void)client;

   struct wlc_view *view;
   if (!(view = convert_from_wlc_handle((wlc_handle)wl_resource_get_user_data(resource), "view")))
      return;

   view->surface_pending.visible = (struct wlc_geometry){ { x, y }, { width, height } };
   wlc_view_update(view);
}

static const struct zxdg_surface_v6_interface zxdg_surface_v6_implementation = {
   .destroy = wlc_cb_resource_destructor,
   .get_toplevel = xdg_cb_surface_get_toplevel,
   .get_popup = xdg_cb_surface_get_popup,
   .ack_configure = xdg_cb_surface_ack_configure,
   .set_window_geometry = xdg_cb_surface_set_window_geometry,
};

static void
xdg_cb_shell_get_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource)
{
   struct wlc_surface *surface;
   struct wlc_xdg_shell *xdg_shell;
   if (!(xdg_shell = wl_resource_get_user_data(resource)) || !(surface = convert_from_wl_resource(surface_resource, "surface")))
      return;

   wlc_resource r;
   if (!(r = wlc_resource_create(&xdg_shell->surfaces, client, &zxdg_surface_v6_interface, wl_resource_get_version(resource), 1, id)))
      return;

   struct xdg_surface *xdg_surface = convert_from_wlc_resource(r, "xdg-surface");
   assert(xdg_surface);
   xdg_surface->surface = wlc_resource_from_wl_resource(surface_resource);

   wlc_resource_implement(r, &zxdg_surface_v6_implementation, xdg_shell);
}

static void
xdg_cb_create_positioner(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   (void)client, (void)id;
   STUB(resource);
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

static const struct zxdg_shell_v6_interface zxdg_shell_v6_implementation = {
   .destroy = xdg_cb_destroy,
   .create_positioner = xdg_cb_create_positioner,
   .get_xdg_surface = xdg_cb_shell_get_surface,
   .pong = xdg_cb_shell_pong,
};

static void
xdg_shell_bind(struct wl_client *client, void *data, unsigned int version, unsigned int id)
{
   struct wl_resource *resource;
   if (!(resource = wl_resource_create_checked(client, &zxdg_shell_v6_interface, version, 1, id)))
      return;

   wl_resource_set_implementation(resource, &zxdg_shell_v6_implementation, data, NULL);
}

void
wlc_xdg_shell_release(struct wlc_xdg_shell *xdg_shell)
{
   if (!xdg_shell)
      return;

   if (xdg_shell->wl.xdg_shell)
      wl_global_destroy(xdg_shell->wl.xdg_shell);

   wlc_source_release(&xdg_shell->surfaces);
   wlc_source_release(&xdg_shell->toplevels);
   wlc_source_release(&xdg_shell->popups);
   memset(xdg_shell, 0, sizeof(struct wlc_xdg_shell));
}

bool
wlc_xdg_shell(struct wlc_xdg_shell *xdg_shell)
{
   assert(xdg_shell);
   memset(xdg_shell, 0, sizeof(struct wlc_xdg_shell));

   if (!(xdg_shell->wl.xdg_shell = wl_global_create(wlc_display(), &zxdg_shell_v6_interface, 1, xdg_shell, xdg_shell_bind)))
      goto xdg_shell_interface_fail;

   if (!wlc_source(&xdg_shell->surfaces, "xdg-surface", NULL, NULL, 32, sizeof(struct xdg_surface)) ||
       !wlc_source(&xdg_shell->toplevels, "xdg-toplevel", NULL, NULL, 32, sizeof(struct wlc_resource)) ||
       !wlc_source(&xdg_shell->popups, "xdg-popup", NULL, NULL, 32, sizeof(struct wlc_resource)))
      goto fail;

   return xdg_shell;

xdg_shell_interface_fail:
   wlc_log(WLC_LOG_WARN, "Failed to bind xdg_shell interface");
fail:
   wlc_xdg_shell_release(xdg_shell);
   return NULL;
}
