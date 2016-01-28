#include <stdlib.h>
#include <assert.h>
#include "internal.h"
#include "platform/context/context.h"
#include "render.h"
#include "gles2.h"

void
wlc_render_resolution(struct wlc_render *render, struct wlc_context *bound, const struct wlc_size *mode, const struct wlc_size *resolution)
{
   assert(render && bound && mode && resolution);

   if (!render->api.resolution || !wlc_context_bind(bound))
      return;

   render->api.resolution(render->render, mode, resolution);
}

void
wlc_render_surface_destroy(struct wlc_render *render, struct wlc_context *bound, struct wlc_surface *surface)
{
   assert(render && bound && surface);

   if (!render->api.surface_destroy || !wlc_context_bind(bound))
      return;

   render->api.surface_destroy(render->render, bound, surface);
}

bool
wlc_render_surface_attach(struct wlc_render *render, struct wlc_context *bound, struct wlc_surface *surface, struct wlc_buffer *buffer)
{
   assert(render && bound && surface);

   if (!render->api.surface_attach || !wlc_context_bind(bound))
      return false;

   return render->api.surface_attach(render->render, bound, surface, buffer);
}

void
wlc_render_view_paint(struct wlc_render *render, struct wlc_context *bound, struct wlc_view *view)
{
   assert(render && view);

   if (!render->api.view_paint || !wlc_context_bind(bound))
      return;

   render->api.view_paint(render->render, view);
}

void
wlc_render_surface_paint(struct wlc_render *render, struct wlc_context *bound, struct wlc_surface *surface, const struct wlc_geometry *geometry)
{
   assert(render);

   if (!render->api.surface_paint || !wlc_context_bind(bound))
      return;

   render->api.surface_paint(render->render, surface, geometry);
}

void
wlc_render_pointer_paint(struct wlc_render *render, struct wlc_context *bound, const struct wlc_point *pos)
{
   assert(render);

   if (!render->api.pointer_paint || !wlc_context_bind(bound))
      return;

   render->api.pointer_paint(render->render, pos);
}

void
wlc_render_read_pixels(struct wlc_render *render, struct wlc_context *bound, enum wlc_pixel_format format, const struct wlc_geometry *geometry, struct wlc_geometry *out_geometry, void *out_data)
{
   assert(render);

   if (!render->api.read_pixels || !wlc_context_bind(bound))
      return;

   render->api.read_pixels(render->render, format, geometry, out_geometry, out_data);
}

void
wlc_render_write_pixels(struct wlc_render *render, struct wlc_context *bound, enum wlc_pixel_format format, const struct wlc_geometry *geometry, const void *data)
{
   assert(render);

   if (!render->api.write_pixels || !wlc_context_bind(bound))
      return;

   render->api.write_pixels(render->render, format, geometry, data);
}

void
wlc_render_flush_fakefb(struct wlc_render *render, struct wlc_context *bound)
{
   assert(render);

   if (!render->api.flush_fakefb || !wlc_context_bind(bound))
      return;

   render->api.flush_fakefb(render->render);
}

void
wlc_render_clear(struct wlc_render *render, struct wlc_context *bound)
{
   assert(render);

   if (!render->api.clear || !wlc_context_bind(bound))
      return;

   render->api.clear(render->render);
}

void
wlc_render_release(struct wlc_render *render, struct wlc_context *bound)
{
   assert(render);

   if (render->api.terminate) {
      if (!wlc_context_bind(bound))
         return;

      render->api.terminate(render->render);
   }

   memset(render, 0, sizeof(struct wlc_render));
}

bool
wlc_render(struct wlc_render *render, struct wlc_context *context)
{
   assert(render && context);
   memset(render, 0, sizeof(struct wlc_render));

   if (!wlc_context_bind(context))
      return NULL;

   void* (*constructor[])(struct wlc_render_api*) = {
      wlc_gles2,
      NULL
   };

   for (uint32_t i = 0; constructor[i]; ++i) {
      if ((render->render = constructor[i](&render->api)))
         return true;
   }

   wlc_log(WLC_LOG_WARN, "Could not initialize any rendering backend");
   wlc_render_release(render, context);
   return false;
}
