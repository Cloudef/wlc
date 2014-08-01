#ifndef _WLC_EGL_H_
#define _WLC_EGL_H_

#include <stdbool.h>

struct wlc_context;
struct wlc_backend;

bool wlc_egl_init(struct wlc_backend *backend, struct wlc_context *out_context);

#endif /* _WLC_EGL_H_ */
