#ifndef _WLC_RENDER_H_
#define _WLC_RENDER_H_

#include <pixman.h>

struct wlc_context;
struct wlc_surface;
struct wlc_buffer;

struct wlc_render {
   void (*terminate)(void);

   struct {
      void (*destroy)(struct wlc_surface *surface);
      void (*attach)(struct wlc_surface *surface, struct wlc_buffer *buffer);
      void (*render)(struct wlc_surface *surface);
      void (*clear)(void);
      void (*swap)(void);
   } api;
};

void wlc_render_terminate(struct wlc_render *render);
struct wlc_render* wlc_render_init(struct wlc_context *context);

#endif /* _WLC_RENDER_H_ */
