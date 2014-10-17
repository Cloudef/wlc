#ifndef _WLC_GEOMETRY_H_
#define _WLC_GEOMETRY_H_

#include <stdint.h>

// XXX: add wlc_size and replace struct { w, h } stuff with it

struct wlc_geometry {
   int32_t x, y, w, h;
};

#endif /* _WLC_GEOMETRY_H_ */
