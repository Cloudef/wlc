#include "string.h"

#include <stdlib.h>
#include <string.h>

static char*
c_strdup(const char *str)
{
   size_t size = strlen(str);
   char *cpy = calloc(1, size + 1);
   return (cpy ? memcpy(cpy, str, size) : NULL);
}

void
wlc_string_set(struct wlc_string *string, const char *data, bool is_heap)
{
   if (string->is_heap && string->data) {
      free(string->data);
      string->data = NULL;
   }

   string->is_heap = is_heap;
   string->data = (data && is_heap ? c_strdup(data) : (char*)data);
}
