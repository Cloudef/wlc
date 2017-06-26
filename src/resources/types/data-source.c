#include "data-source.h"
#include <string.h>
#include <assert.h>
#include <chck/string/string.h>

void
wlc_data_source_release(struct wlc_data_source *source)
{
   if (!source)
      return;

   chck_iter_pool_for_each_call(&source->types, chck_string_release);
   chck_iter_pool_release(&source->types);
}

bool
wlc_data_source(struct wlc_data_source *source, const struct wlc_data_source_impl *impl)
{
   assert(source);
   source->impl = impl;
   return chck_iter_pool(&source->types, 32, 0, sizeof(struct chck_string));
}
