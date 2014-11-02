#include "wlc.h"
#include "render.h"
#include "gles2.h"

#include <stdlib.h>
#include <assert.h>

struct wlc_render {
   void *render; // internal renderer context (OpenGL, etc)
   struct wlc_render_api api;
};

bool
wlc_render_bind(struct wlc_render *render, struct wlc_output *output)
{
   assert(render && output);
   return render->api.bind(render->render, output);
}

void
wlc_render_surface_destroy(struct wlc_render *render, struct wlc_surface *surface)
{
   assert(render && surface);
   render->api.surface_destroy(render->render, surface);
}

bool
wlc_render_surface_attach(struct wlc_render *render, struct wlc_surface *surface, struct wlc_buffer *buffer)
{
   assert(render && surface);
   return render->api.surface_attach(render->render, surface, buffer);
}

void
wlc_render_view_paint(struct wlc_render *render, struct wlc_view *view)
{
   assert(render && view);
   render->api.view_paint(render->render, view);
}

void
wlc_render_surface_paint(struct wlc_render *render, struct wlc_surface *surface, struct wlc_origin *pos)
{
   assert(render);
   render->api.surface_paint(render->render, surface, pos);
}

void
wlc_render_pointer_paint(struct wlc_render *render, struct wlc_origin *pos)
{
   assert(render);
   render->api.pointer_paint(render->render, pos);
}

void
wlc_render_read_pixels(struct wlc_render *render, struct wlc_geometry *geometry, void *out_data)
{
   assert(render);
   render->api.read_pixels(render->render, geometry, out_data);
}

void
wlc_render_clear(struct wlc_render *render)
{
   assert(render);
   render->api.clear(render->render);
}

void
wlc_render_swap(struct wlc_render *render)
{
   assert(render);
   render->api.swap(render->render);
}

void
wlc_render_free(struct wlc_render *render)
{
   assert(render);
   render->api.terminate(render->render);
   free(render);
}

struct wlc_render*
wlc_render_new(struct wlc_context *context)
{
   assert(context);

   struct wlc_render *render;
   if (!(render = calloc(1, sizeof(struct wlc_render))))
      goto out_of_memory;

   void* (*constructor[])(struct wlc_context*, struct wlc_render_api*) = {
      wlc_gles2_new,
      NULL
   };

   for (int i = 0; constructor[i]; ++i) {
      if ((render->render = constructor[i](context, &render->api)))
         return render;
   }

   wlc_log(WLC_LOG_WARN, "Could not initialize any rendering backend");
   wlc_render_free(render);
   return NULL;

out_of_memory:
   wlc_log(WLC_LOG_WARN, "Out of memory");
   return NULL;
}
