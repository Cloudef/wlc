#include "internal.h"
#include "visibility.h"
#include "macros.h"
#include "resources/types/surface.h"
#include "compositor/output.h"
#include "compositor/view.h"
#include "compositor/compositor.h"
#include "compositor/seat/seat.h"
#include "compositor/seat/pointer.h"
#include "platform/context/context.h"
#include "platform/context/egl.h"
#include "platform/render/render.h"
#include <wlc/wlc-render.h>

static struct wlc_output*
active_output(struct wlc_pointer *pointer)
{
   struct wlc_seat *seat;
   struct wlc_compositor *compositor;
   except((seat = wl_container_of(pointer, seat, pointer)) && (compositor = wl_container_of(seat, compositor, seat)));
   return convert_from_wlc_handle(compositor->active.output, "output");
}

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

WLC_API enum wlc_renderer
wlc_output_get_renderer(wlc_handle output)
{
   struct wlc_output *o;
   if (!(o = convert_from_wlc_handle(output, "output")))
      return WLC_NO_RENDERER;

   return o->render.api.renderer_type;
}

WLC_API bool
wlc_surface_get_textures(wlc_resource surface, uint32_t out_textures[], enum wlc_surface_format *out_format)
{
   struct wlc_surface *surf;
   if (!(surf = convert_from_wlc_resource(surface, "surface")))
      return false;

   memcpy(out_textures, surf->textures, 3 * sizeof(surf->textures[0]));
   *out_format = surf->format;
   return true;
}

static void
surface_flush_frame_callbacks_recursive(struct wlc_surface *surface, struct wlc_output *output)
{
   wlc_resource *r;
   chck_iter_pool_for_each(&surface->commit.frame_cbs, r)
      chck_iter_pool_push_back(&output->callbacks, r);
   chck_iter_pool_flush(&surface->commit.frame_cbs);

   wlc_resource *sub;
   struct wlc_surface *subsurface;
   chck_iter_pool_for_each(&surface->subsurface_list, sub)
      if ((subsurface = convert_from_wlc_resource(*sub, "surface")))
         surface_flush_frame_callbacks_recursive(subsurface, output);
}

WLC_API void
wlc_surface_flush_frame_callbacks(wlc_resource surface)
{
   struct wlc_surface *surf;
   struct wlc_output *output;
   if (!(surf = convert_from_wlc_resource(surface, "surface")) ||
         !(output = convert_from_wlc_handle(surf->output, "output"))) {
      return;
   }

   surface_flush_frame_callbacks_recursive(surf, output);

   struct wlc_view *v;
   if ((v = convert_from_wlc_handle(surf->parent_view, "view")))
      wlc_view_commit_state(v, &v->pending, &v->commit);
}

WLC_API bool 
wlc_get_active_pointer(wlc_handle out, uint32_t out_textures[3], enum wlc_surface_format *out_format, struct wlc_point *tip, struct wlc_size *size) 
{
   out_textures[0] = 0;
   *out_format = INVALID;
   
   struct wlc_output *output;
   if (!(output = convert_from_wlc_handle(out, "output"))) {
      return false;
   }
   
   struct wlc_pointer *pointer = &wlc_get_compositor()->seat.pointer;
   if (!pointer) {
      return false;
   }
   
   if (output != active_output(pointer)) {
      return false;
   }
   
   struct wlc_surface *surface;
   if ((surface = convert_from_wlc_resource(pointer->surface, "surface"))) {
      if (surface->output != convert_to_wlc_handle(output) && !wlc_surface_attach_to_output(surface, output, wlc_surface_get_buffer(surface))) {
         // fallback
         out_textures[0] = wlc_render_get_pointer_texture(&output->render, size, out_format);
         tip->x = 0;
         tip->y = 0;
         return false;
	  } else {
         tip->x = pointer->tip.x;
         tip->y = pointer->tip.y;
         size->w = surface->size.w;
         size->h = surface->size.h;
         
         memcpy(out_textures, surface->textures, 3 * sizeof(surface->textures[0]));
         *out_format = surface->format;
         
         return true;
     }
   } else {
      // fallback
      out_textures[0] = wlc_render_get_pointer_texture(&output->render, size, out_format);
      tip->x = 0;
      tip->y = 0;
      return false;
   }
}
