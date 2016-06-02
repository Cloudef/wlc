#ifndef _WLC_BACKEND_H_
#define _WLC_BACKEND_H_

#include <wlc/wlc.h>
#include <stdbool.h>
#include "EGL/egl.h"

struct chck_pool;

struct wlc_backend_surface {
   void *internal;
   size_t internal_size;
   EGLNativeDisplayType display;
   EGLNativeWindowType window;

   struct {
      WLC_NONULL void (*terminate)(struct wlc_backend_surface *surface);
      WLC_NONULL void (*sleep)(struct wlc_backend_surface *surface, bool sleep);
      WLC_NONULL bool (*page_flip)(struct wlc_backend_surface *surface);
   } api;
};

struct wlc_backend {
   enum wlc_backend_type type;

   struct {
      WLC_NONULL uint32_t (*update_outputs)(struct chck_pool *outputs);
      void (*terminate)(void);
   } api;
};

WLC_NONULL bool wlc_backend_surface(struct wlc_backend_surface *surface, void (*destructor)(struct wlc_backend_surface*), size_t internal_size);
void wlc_backend_surface_release(struct wlc_backend_surface *surface);

WLC_NONULL uint32_t wlc_backend_update_outputs(struct wlc_backend *backend, struct chck_pool *outputs);
void wlc_backend_release(struct wlc_backend *backend);
WLC_NONULL bool wlc_backend(struct wlc_backend *backend);

#endif /* _WLC_BACKEND_H_ */
