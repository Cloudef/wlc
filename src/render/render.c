#include "render.h"
#include "gles2.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

void
wlc_render_terminate(struct wlc_render *render)
{
   assert(render);

   if (render->terminate)
      render->terminate();

   free(render);
}

struct wlc_render*
wlc_render_init(struct wlc_context *context)
{
   struct wlc_render *render;

   if (!(render = calloc(1, sizeof(struct wlc_render))))
      goto out_of_memory;

   bool (*init[])(struct wlc_context*, struct wlc_render*) = {
      wlc_gles2_init,
      NULL
   };

   for (int i = 0; init[i]; ++i)
      if (init[i](context, render))
         return render;

   fprintf(stderr, "-!- Could not initialize any rendering backend\n");
   free(render);
   return NULL;

out_of_memory:
   fprintf(stderr, "-!- Out of memory\n");
   return NULL;
}
