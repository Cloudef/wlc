#ifndef _WLC_BACKEND_H_
#define _WLC_BACKEND_H_

#include "EGL/egl.h"
#include <stdbool.h>

struct wlc_seat;
struct wlc_keymap;
struct wlc_compositor;

struct wlc_backend {
   const char *name;
   void (*terminate)(void);

   struct {
      EGLNativeDisplayType (*display)(void);
      EGLNativeWindowType (*window)(void);
      bool (*page_flip)(void);
   } api;
};

void wlc_backend_terminate(struct wlc_backend *backend);
struct wlc_backend* wlc_backend_init(struct wlc_compositor *compositor);

#endif /* _WLC_BACKEND_H_ */
