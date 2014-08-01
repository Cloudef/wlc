#include "context.h"
#include "egl.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

void
wlc_context_terminate(struct wlc_context *context)
{
   assert(context);

   if (context->terminate)
      context->terminate();

   free(context);
}

struct wlc_context*
wlc_context_init(struct wlc_backend *backend)
{
   struct wlc_context *context;

   if (!(context = calloc(1, sizeof(struct wlc_context))))
      goto out_of_memory;

   bool (*init[])(struct wlc_backend*, struct wlc_context*) = {
      wlc_egl_init,
      NULL
   };

   for (int i = 0; init[i]; ++i)
      if (init[i](backend, context))
         return context;

   fprintf(stderr, "-!- Could not initialize any context\n");
   free(context);
   return NULL;

out_of_memory:
   fprintf(stderr, "-!- Out of memory\n");
   return NULL;
}
