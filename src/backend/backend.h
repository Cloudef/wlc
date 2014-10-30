#ifndef _WLC_BACKEND_H_
#define _WLC_BACKEND_H_

#include "EGL/egl.h"
#include <stdbool.h>

struct wlc_compositor;
struct wlc_output;

struct wlc_backend_surface {
   void *internal;
   EGLNativeDisplayType display;
   EGLNativeWindowType window;
   size_t internal_size;

   struct {
      void (*terminate)(struct wlc_backend_surface *surface);
      bool (*page_flip)(struct wlc_backend_surface *surface);
   } api;
};

struct wlc_backend {
   struct {
      void (*terminate)(void);
   } api;
};

struct wlc_backend_surface* wlc_backend_surface_new(void (*destructor)(struct wlc_backend_surface*), size_t internal_size);
void wlc_backend_surface_free(struct wlc_backend_surface *surface);

void wlc_backend_terminate(struct wlc_backend *backend);
struct wlc_backend* wlc_backend_init(struct wlc_compositor *compositor);

#endif /* _WLC_BACKEND_H_ */
