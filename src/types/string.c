#include "string.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>

static char*
ccopy(const char *str, size_t len)
{
   char *cpy = calloc(1, len + 1);
   return (cpy ? memcpy(cpy, str, len) : NULL);
}

bool
wlc_string_set(struct wlc_string *string, const char *data, bool is_heap)
{
   assert(string);

   char *copy = NULL;
   if (is_heap && data && !(copy = ccopy(data, strlen(data))))
      return false;

   wlc_string_release(string);
   string->is_heap = (copy ? true : false);
   string->data = (copy ? copy : (char*)data);
   return true;
}

bool
wlc_string_set_with_length(struct wlc_string *string, const char *data, size_t length)
{
   assert(string);

   char *copy = NULL;
   if (data && length > 0 && !(copy = ccopy(data, length)))
      return false;

   wlc_string_release(string);
   string->is_heap = (copy ? true : false);
   string->data = copy;
   return true;
}

void
wlc_string_release(struct wlc_string *string)
{
   assert(string);

   if (string->is_heap && string->data)
      free(string->data);

   string->data = NULL;
}
