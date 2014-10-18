#ifndef _WLC_RENDER_H_
#define _WLC_RENDER_H_

#include <stdint.h>
#include <stdbool.h>

struct wlc_context;
struct wlc_surface;
struct wlc_buffer;
struct wlc_view;
struct wlc_output;
struct wlc_render;
struct ctx;

struct wlc_render_api {
   void (*terminate)(struct ctx *render);
   bool (*bind)(struct ctx *render, struct wlc_output *output);
   void (*surface_destroy)(struct ctx *render, struct wlc_surface *surface);
   bool (*surface_attach)(struct ctx *render, struct wlc_surface *surface, struct wlc_buffer *buffer);
   void (*view_paint)(struct ctx *render, struct wlc_view *view);
   void (*pointer_paint)(struct ctx *render, int32_t x, int32_t y);
   void (*clear)(struct ctx *render);
   void (*swap)(struct ctx *render);
};

bool wlc_render_bind(struct wlc_render *render, struct wlc_output *output);
void wlc_render_surface_destroy(struct wlc_render *render, struct wlc_surface *surface);
bool wlc_render_surface_attach(struct wlc_render *render, struct wlc_surface *surface, struct wlc_buffer *buffer);
void wlc_render_view_paint(struct wlc_render *render, struct wlc_view *view);
void wlc_render_pointer_paint(struct wlc_render *render, int32_t x, int32_t y);
void wlc_render_clear(struct wlc_render *render);
void wlc_render_swap(struct wlc_render *render);

void wlc_render_free(struct wlc_render *render);
struct wlc_render* wlc_render_new(struct wlc_context *context);

#endif /* _WLC_RENDER_H_ */
