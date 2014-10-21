#ifndef _WLC_GEOMETRY_H_
#define _WLC_GEOMETRY_H_

#include <stdint.h>
#include <stdbool.h>

// FIXME: make others use these as well instead of own structs

struct wlc_origin {
   int32_t x, y;
};

struct wlc_size {
   uint32_t w, h;
};

struct wlc_geometry {
   struct wlc_origin origin;
   struct wlc_size size;
};

static const struct wlc_origin wlc_origin_zero = { 0, 0 };
static const struct wlc_size wlc_size_zero = { 0, 0 };
static const struct wlc_geometry wlc_geometry_zero = { { 0, 0 }, { 0, 0 } };

static inline bool
wlc_origin_equals(const struct wlc_origin *a, const struct wlc_origin *b)
{
   return (a->x == b->x && a->y == b->y);
}

static inline bool
wlc_size_equals(const struct wlc_size *a, const struct wlc_size *b)
{
   return (a->w == b->w && a->h == b->h);
}

static inline bool
wlc_geometry_equals(const struct wlc_geometry *a, const struct wlc_geometry *b)
{
   return (wlc_origin_equals(&a->origin, &b->origin) && wlc_size_equals(&a->size, &b->size));
}

#endif /* _WLC_GEOMETRY_H_ */
