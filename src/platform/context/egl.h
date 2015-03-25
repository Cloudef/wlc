#ifndef _WLC_EGL_H_
#define _WLC_EGL_H_

struct wlc_context_api;
struct wlc_backend_surface;

void* wlc_egl(struct wlc_backend_surface *bsurface, struct wlc_context_api *api);

#endif /* _WLC_EGL_H_ */
