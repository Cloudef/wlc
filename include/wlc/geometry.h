#ifndef _WLC_GEOMETRY_H_
#define _WLC_GEOMETRY_H_

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
struct wlc_origin {
   int32_t x, y;
};

/** Fixed 2D size */
struct wlc_size {
   uint32_t w, h;
};

/** Fixed 2D point, size pair */
struct wlc_geometry {
   struct wlc_origin origin;
   struct wlc_size size;
};

static const struct wlc_origin wlc_origin_zero = { 0, 0 };
static const struct wlc_size wlc_size_zero = { 0, 0 };
static const struct wlc_geometry wlc_geometry_zero = { { 0, 0 }, { 0, 0 } };

static inline void
wlc_origin_min(const struct wlc_origin *a, const struct wlc_origin *b, struct wlc_origin *out)
{
   assert(a && b && out);
   out->x = MIN(a->x, b->x);
   out->y = MIN(a->y, b->y);
}

static inline void
wlc_origin_max(const struct wlc_origin *a, const struct wlc_origin *b, struct wlc_origin *out)
{
   assert(a && b && out);
   out->x = MAX(a->x, b->x);
   out->y = MAX(a->y, b->y);
}

static inline void
wlc_size_min(const struct wlc_size *a, const struct wlc_size *b, struct wlc_size *out)
{
   assert(a && b && out);
   out->w = MIN(a->w, b->w);
   out->h = MIN(a->h, b->h);
}

static inline void
wlc_size_max(const struct wlc_size *a, const struct wlc_size *b, struct wlc_size *out)
{
   assert(a && b && out);
   out->w = MAX(a->w, b->w);
   out->h = MAX(a->h, b->h);
}

static inline bool
wlc_origin_equals(const struct wlc_origin *a, const struct wlc_origin *b)
{
   assert(a && b);
   return !memcmp(a, b, sizeof(struct wlc_origin));
}

static inline bool
wlc_size_equals(const struct wlc_size *a, const struct wlc_size *b)
{
   assert(a && b);
   return !memcmp(a, b, sizeof(struct wlc_size));
}

static inline bool
wlc_geometry_equals(const struct wlc_geometry *a, const struct wlc_geometry *b)
{
   assert(a && b);
   return !memcmp(a, b, sizeof(struct wlc_geometry));
}

static inline bool
wlc_geometry_contains(const struct wlc_geometry *a, const struct wlc_geometry *b)
{
   assert(a && b);
   return (a->origin.x <= b->origin.x && a->origin.y <= b->origin.y &&
           a->origin.x + (int32_t)a->size.w >= b->origin.x + (int32_t)b->size.w && a->origin.y + (int32_t)a->size.h >= b->origin.y + (int32_t)b->size.h);
}

#endif /* _WLC_GEOMETRY_H_ */
