#ifndef _WLC_BACKEND_H_
#define _WLC_BACKEND_H_

#include "EGL/egl.h"
#include <stdbool.h>

struct wlc_compositor;
struct wlc_output;

struct wlc_backend {
   const char *name;
   void (*terminate)(void);

   struct {
      EGLNativeDisplayType (*display)(struct wlc_output*);
      EGLNativeWindowType (*window)(struct wlc_output*);
      bool (*page_flip)(struct wlc_output*);
   } api;
};

void wlc_backend_terminate(struct wlc_backend *backend);
struct wlc_backend* wlc_backend_init(struct wlc_compositor *compositor);

#endif /* _WLC_BACKEND_H_ */
