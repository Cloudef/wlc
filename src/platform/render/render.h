#ifndef _WLC_RENDER_API_H_
#define _WLC_RENDER_API_H_

#include <stdint.h>
#include <stdbool.h>
#include <wlc/wlc-render.h>
#include "resources/resources.h"

struct wlc_context;
struct wlc_surface;
struct wlc_buffer;
struct wlc_view;
struct wlc_output;
struct wlc_render;
struct wlc_point;
struct wlc_geometry;
struct ctx;

struct wlc_render_api {
   WLC_NONULL void (*terminate)(struct ctx *render);
   WLC_NONULL void (*resolution)(struct ctx *render, const struct wlc_size *mode, const struct wlc_size *resolution);
   WLC_NONULL void (*surface_destroy)(struct ctx *render, struct wlc_context *bound, struct wlc_surface *surface);
   WLC_NONULLV(1,2,3) bool (*surface_attach)(struct ctx *render, struct wlc_context *bound, struct wlc_surface *surface, struct wlc_buffer *buffer);
   WLC_NONULL void (*view_paint)(struct ctx *render, struct wlc_view *view);
   WLC_NONULL void (*surface_paint)(struct ctx *render, struct wlc_surface *surface, const struct wlc_geometry *geometry);
   WLC_NONULL void (*pointer_paint)(struct ctx *render, const struct wlc_point *pos);
   WLC_NONULL void (*read_pixels)(struct ctx *render, enum wlc_pixel_format format, const struct wlc_geometry *geometry, struct wlc_geometry *out_geometry, void *out_data);
   WLC_NONULL void (*write_pixels)(struct ctx *render, enum wlc_pixel_format format, const struct wlc_geometry *geometry, const void *data);
   WLC_NONULL void (*flush_fakefb)(struct ctx *render);
   WLC_NONULL void (*clear)(struct ctx *render);
};

struct wlc_render {
   void *render; // internal renderer context (OpenGL, etc)
   struct wlc_render_api api;
};

WLC_NONULL void wlc_render_resolution(struct wlc_render *render, struct wlc_context *bound, const struct wlc_size *mode, const struct wlc_size *resolution);
WLC_NONULL void wlc_render_surface_destroy(struct wlc_render *render, struct wlc_context *bound, struct wlc_surface *surface);
WLC_NONULLV(1,2,3) bool wlc_render_surface_attach(struct wlc_render *render, struct wlc_context *bound, struct wlc_surface *surface, struct wlc_buffer *buffer);
WLC_NONULL void wlc_render_view_paint(struct wlc_render *render, struct wlc_context *bound, struct wlc_view *view);
WLC_NONULL void wlc_render_surface_paint(struct wlc_render *render, struct wlc_context *bound, struct wlc_surface *surface, const struct wlc_geometry *geometry);
WLC_NONULL void wlc_render_pointer_paint(struct wlc_render *render, struct wlc_context *bound, const struct wlc_point *pos);
WLC_NONULL void wlc_render_read_pixels(struct wlc_render *render, struct wlc_context *bound, enum wlc_pixel_format format, const struct wlc_geometry *geometry, struct wlc_geometry *out_geometry, void *out_data);
WLC_NONULL void wlc_render_write_pixels(struct wlc_render *render, struct wlc_context *bound, enum wlc_pixel_format format, const struct wlc_geometry *geometry, const void *data);
WLC_NONULL void wlc_render_flush_fakefb(struct wlc_render *render, struct wlc_context *bound); // only relevant to GLES2
WLC_NONULL void wlc_render_clear(struct wlc_render *render, struct wlc_context *bound);
void wlc_render_release(struct wlc_render *render, struct wlc_context *context);
WLC_NONULL bool wlc_render(struct wlc_render *render, struct wlc_context *context);

#endif /* _WLC_RENDER_API_H_ */
