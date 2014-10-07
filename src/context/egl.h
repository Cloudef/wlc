#ifndef _WLC_EGL_H_
#define _WLC_EGL_H_

#include <stdbool.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

struct wlc_compositor;
struct wlc_context;
struct wlc_backend;
struct wlc_output;
struct wl_resource;

EGLBoolean wlc_egl_query_buffer(struct wl_resource *buffer, EGLint attribute, EGLint *value);
EGLImageKHR wlc_egl_create_image(struct wlc_output *output, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list);
EGLBoolean wlc_egl_destroy_image(EGLImageKHR image);
bool wlc_egl_init(struct wlc_compositor *compositor, struct wlc_backend *backend, struct wlc_context *out_context);

#endif /* _WLC_EGL_H_ */
