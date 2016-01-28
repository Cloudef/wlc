#include "internal.h"
#include "visibility.h"
#include "resources/types/surface.h"
#include "compositor/output.h"
#include "compositor/view.h"
#include "platform/render/render.h"
#include <wlc/wlc-render.h>

WLC_API void
wlc_surface_render(wlc_resource surface, const struct wlc_geometry *geometry)
{
   assert(geometry);

   struct wlc_output *o;
   if (!(o = wlc_get_rendering_output()))
      return;

   wlc_output_render_surface(o, convert_from_wlc_resource(surface, "surface"), geometry, &o->callbacks);
}

WLC_API void
wlc_pixels_write(enum wlc_pixel_format format, const struct wlc_geometry *geometry, const void *data)
{
   assert(geometry && data);

   struct wlc_output *o;
   if (!(o = wlc_get_rendering_output()))
      return;

   wlc_render_write_pixels(&o->render, &o->context, format, geometry, data);
}

WLC_API void
wlc_pixels_read(enum wlc_pixel_format format, const struct wlc_geometry *geometry, struct wlc_geometry *out_geometry, void *out_data)
{
   assert(geometry && out_geometry && out_data);

   struct wlc_output *o;
   if (!(o = wlc_get_rendering_output()))
      return;

   wlc_render_read_pixels(&o->render, &o->context, format, geometry, out_geometry, out_data);
}

WLC_API void
wlc_output_schedule_render(wlc_handle output)
{
   struct wlc_output *o;
   if (!(o = convert_from_wlc_handle(output, "output")))
      return;

   wlc_output_schedule_repaint(o);
}
