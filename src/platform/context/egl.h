#ifndef _WLC_EGL_H_
#define _WLC_EGL_H_

#include <EGL/egl.h>
#include <EGL/eglext.h>

struct wlc_context_api;
struct wlc_backend_surface;

EGLDeviceEXT get_egl_device(void);

void* wlc_egl(struct wlc_backend_surface *bsurface, struct wlc_context_api *api);

#endif /* _WLC_EGL_H_ */
