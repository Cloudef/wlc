#ifndef _WLC_KEYMAP_H_
#define _WLC_KEYMAP_H_

#include <stdint.h>

struct xkb_keymap;

struct wlc_keymap {
   struct xkb_keymap *keymap;
   uint32_t format;
   uint32_t size;
   int32_t fd;
   char *area;
};

void wlc_keymap_free(struct wlc_keymap *keymap);
struct wlc_keymap* wlc_keymap_new(void);

#endif /* _WLC_KEYMAP_H_ */
