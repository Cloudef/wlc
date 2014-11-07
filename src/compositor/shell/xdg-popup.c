#include "xdg-popup.h"
#include "macros.h"

#include "compositor/view.h"

#include <stdlib.h>
#include <assert.h>

#include <wayland-server.h>
#include "wayland-xdg-shell-server-protocol.h"

static void
xdg_cb_popup_destroy(struct wl_client *wl_client, struct wl_resource *resource)
{
   (void)wl_client, (void)resource;
   wl_resource_destroy(resource);
}

const struct xdg_popup_interface xdg_popup_implementation = {
   .destroy = xdg_cb_popup_destroy,
};

static void
xdg_cb_popup_destructor(struct wl_resource *resource)
{
   assert(resource);
   struct wlc_view *view = wl_resource_get_user_data(resource);
   view->xdg_popup.resource = NULL;
   wlc_xdg_popup_release(&view->xdg_popup);
}

void
wlc_xdg_popup_implement(struct wlc_xdg_popup *xdg_popup, struct wlc_view *view, struct wl_resource *resource)
{
   assert(xdg_popup);

   if (xdg_popup->resource == resource)
      return;

   if (xdg_popup->resource)
      wl_resource_destroy(xdg_popup->resource);

   xdg_popup->resource = resource;
   wl_resource_set_implementation(xdg_popup->resource, &xdg_popup_implementation, view, xdg_cb_popup_destructor);
}

void
wlc_xdg_popup_release(struct wlc_xdg_popup *xdg_popup)
{
   assert(xdg_popup);

   if (xdg_popup->resource)
      wl_resource_destroy(xdg_popup->resource);
}
