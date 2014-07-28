#ifndef _WLC_STRING_H_
#define _WLC_STRING_H_

#include <stdbool.h>

struct wlc_string {
   char *data;
   bool is_heap;
};

void wlc_string_set(struct wlc_string *string, const char *data, bool is_heap);

#endif /* _WLC_STRING_H_ */
