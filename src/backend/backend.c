#include "backend.h"
#include "x11.h"
#include "drm.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

void
wlc_backend_terminate(struct wlc_backend *context)
{
   assert(context);

   if (context->terminate)
      context->terminate();

   free(context);
}

struct wlc_backend*
wlc_backend_init(void)
{
   struct wlc_backend *backend;

   if (!(backend = calloc(1, sizeof(struct wlc_backend))))
      goto out_of_memory;

   bool (*init[])(struct wlc_backend*) = {
      wlc_x11_init,
      wlc_drm_init,
      NULL
   };

   for (int i = 0; init[i]; ++i)
      if (init[i](backend))
         return backend;

   fprintf(stderr, "-!- Could not initialize any backend\n");
   free(backend);
   return NULL;

out_of_memory:
   fprintf(stderr, "-!- Out of memory\n");
   return NULL;
}
