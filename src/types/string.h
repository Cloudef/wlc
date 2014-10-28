#ifndef _WLC_STRING_H_
#define _WLC_STRING_H_

#include <stdbool.h>
#include <stddef.h>

struct wlc_string {
   char *data;
   bool is_heap;
};

bool wlc_string_set(struct wlc_string *string, const char *data, bool is_heap);
bool wlc_string_set_with_length(struct wlc_string *string, const char *data, size_t len);
void wlc_string_release(struct wlc_string *string);

#endif /* _WLC_STRING_H_ */
