#include "pointer.h"

#include <stdlib.h>
#include <assert.h>

void
wlc_pointer_free(struct wlc_pointer *pointer)
{
   assert(pointer);
   free(pointer);
}

struct wlc_pointer*
wlc_pointer_new(void)
{
   struct wlc_pointer *pointer;

   if (!(pointer = calloc(1, sizeof(struct wlc_pointer))))
      return NULL;

   wl_list_init(&pointer->resource_list);
   return pointer;
}
