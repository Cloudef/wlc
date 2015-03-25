#ifndef _WLC_BACKEND_H_
#define _WLC_BACKEND_H_

#include <stdbool.h>
#include "EGL/egl.h"

struct wlc_output;
struct chck_pool;

struct wlc_backend_surface {
   void *internal;
   size_t internal_size;
   EGLNativeDisplayType display;
   EGLNativeWindowType window;

   struct {
      void (*terminate)(struct wlc_backend_surface *surface);
      void (*sleep)(struct wlc_backend_surface *surface, bool sleep);
      bool (*page_flip)(struct wlc_backend_surface *surface);
   } api;
};

struct wlc_backend {
   struct {
      uint32_t (*update_outputs)(struct chck_pool *outputs);
      void (*terminate)(void);
   } api;
};

bool wlc_backend_surface(struct wlc_backend_surface *surface, void (*destructor)(struct wlc_backend_surface*), size_t internal_size);
void wlc_backend_surface_release(struct wlc_backend_surface *surface);

uint32_t wlc_backend_update_outputs(struct wlc_backend *backend, struct chck_pool *outputs);
void wlc_backend_release(struct wlc_backend *backend);
bool wlc_backend(struct wlc_backend *backend);

#endif /* _WLC_BACKEND_H_ */
