#ifndef _WLC_GEOMETRY_H_
#define _WLC_GEOMETRY_H_

#ifdef __cplusplus
extern "C" {
#endif 

#include <wlc/defines.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#ifndef MIN
#  define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#  define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

/** Fixed 2D point */
struct wlc_point {
   int32_t x, y;
};

/** wlc_origin is depreacted in favor of wlc_point */
#define wlc_origin wlc_point __attribute__((deprecated("wlc_origin is deprecated, use wlc_point instead")))

/** Fixed 2D size */
struct wlc_size {
   uint32_t w, h;
};

/** Fixed 2D point, size pair */
struct wlc_geometry {
   struct wlc_point origin;
   struct wlc_size size;
};

static const struct wlc_point wlc_origin_zero = { 0, 0 };
static const struct wlc_point wlc_point_zero = { 0, 0 };
static const struct wlc_size wlc_size_zero = { 0, 0 };
static const struct wlc_geometry wlc_geometry_zero = { { 0, 0 }, { 0, 0 } };

WLC_NONULL static inline void
wlc_point_min(const struct wlc_point *a, const struct wlc_point *b, struct wlc_point *out)
{
   assert(a && b && out);
   out->x = MIN(a->x, b->x);
   out->y = MIN(a->y, b->y);
}

WLC_NONULL static inline void
wlc_point_max(const struct wlc_point *a, const struct wlc_point *b, struct wlc_point *out)
{
   assert(a && b && out);
   out->x = MAX(a->x, b->x);
   out->y = MAX(a->y, b->y);
}

WLC_NONULL static inline void
wlc_size_min(const struct wlc_size *a, const struct wlc_size *b, struct wlc_size *out)
{
   assert(a && b && out);
   out->w = MIN(a->w, b->w);
   out->h = MIN(a->h, b->h);
}

WLC_NONULL static inline void
wlc_size_max(const struct wlc_size *a, const struct wlc_size *b, struct wlc_size *out)
{
   assert(a && b && out);
   out->w = MAX(a->w, b->w);
   out->h = MAX(a->h, b->h);
}

WLC_NONULL WLC_PURE static inline bool
wlc_point_equals(const struct wlc_point *a, const struct wlc_point *b)
{
   assert(a && b);
   return !memcmp(a, b, sizeof(struct wlc_point));
}

WLC_NONULL WLC_PURE static inline bool
wlc_size_equals(const struct wlc_size *a, const struct wlc_size *b)
{
   assert(a && b);
   return !memcmp(a, b, sizeof(struct wlc_size));
}

WLC_NONULL WLC_PURE static inline bool
wlc_geometry_equals(const struct wlc_geometry *a, const struct wlc_geometry *b)
{
   assert(a && b);
   return !memcmp(a, b, sizeof(struct wlc_geometry));
}

WLC_NONULL WLC_PURE static inline bool
wlc_geometry_contains(const struct wlc_geometry *a, const struct wlc_geometry *b)
{
   assert(a && b);
   return (a->origin.x <= b->origin.x && a->origin.y <= b->origin.y &&
           a->origin.x + (int32_t)a->size.w >= b->origin.x + (int32_t)b->size.w && a->origin.y + (int32_t)a->size.h >= b->origin.y + (int32_t)b->size.h);
}

#ifdef __cplusplus
}
#endif 

#endif /* _WLC_GEOMETRY_H_ */
