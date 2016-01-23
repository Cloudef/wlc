#include "internal.h"
#include "visibility.h"
#include "resources/types/surface.h"
#include "compositor/output.h"
#include "compositor/view.h"
#include <wlc/wlc-wayland.h>

WLC_API WLC_PURE struct wl_display*
wlc_get_wl_display(void)
{
   return wlc_display();
}

WLC_API wlc_handle
wlc_handle_from_wl_surface_resource(struct wl_resource *resource)
{
   assert(resource);
   const struct wlc_surface *surface = convert_from_wl_resource(resource, "surface");
   return (surface ? surface->view : 0);
}

WLC_API wlc_handle
wlc_handle_from_wl_output_resource(struct wl_resource *resource)
{
   assert(resource);
   return (wlc_handle)wl_resource_get_user_data(resource);
}

WLC_API wlc_resource
wlc_resource_from_wl_surface_resource(struct wl_resource *resource)
{
   assert(resource);
   return wlc_resource_from_wl_resource(resource);
}

WLC_API const struct wlc_size*
wlc_surface_get_size(wlc_resource surface)
{
   struct wlc_surface *s = convert_from_wlc_resource(surface, "surface");
   return (s ? &s->size : NULL);
}

WLC_API struct wl_resource*
wlc_surface_get_wl_resource(wlc_resource surface)
{
   return wl_resource_from_wlc_resource(surface, "surface");
}

WLC_API void
wlc_surface_render(wlc_resource surface, const struct wlc_geometry *geometry)
{
   wlc_output_render_surface(surface, geometry);
}

WLC_API struct wl_resource*
wlc_view_get_role(wlc_handle view)
{
   const struct wlc_view *v = convert_from_wlc_handle(view, "view");
   return (v ? wl_resource_from_wlc_resource(v->custom_surface, "custom-surface") : 0);
}

WLC_API wlc_resource
wlc_view_get_surface(wlc_handle view)
{
   const struct wlc_view *v = convert_from_wlc_handle(view, "view");
   return (v ? v->surface : 0);
}

WLC_API struct wl_client*
wlc_view_get_wl_client(wlc_handle view)
{
   return wlc_view_get_client_ptr(convert_from_wlc_handle(view, "view"));
}
