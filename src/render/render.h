#ifndef _WLC_RENDER_H_
#define _WLC_RENDER_H_

#include <stdint.h>
#include <stdbool.h>

struct wlc_context;
struct wlc_surface;
struct wlc_buffer;
struct wlc_view;
struct wlc_output;

struct wlc_render {
   void (*terminate)(void);

   struct {
      bool (*bind)(struct wlc_output *output);
      void (*destroy)(struct wlc_surface *surface);
      void (*attach)(struct wlc_surface *surface, struct wlc_buffer *buffer);
      void (*render)(struct wlc_view *view);
      void (*pointer)(int32_t x, int32_t y);
      void (*clear)(void);
      void (*swap)(void);
      void (*resolution)(struct wlc_output *output, int32_t width, int32_t height);
   } api;
};

void wlc_render_terminate(struct wlc_render *render);
struct wlc_render* wlc_render_init(struct wlc_context *context);

#endif /* _WLC_RENDER_H_ */
