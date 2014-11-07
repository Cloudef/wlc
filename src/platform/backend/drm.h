#ifndef _WLC_DRM_H_
#define _WLC_DRM_H_

#include <stdbool.h>

struct wlc_backend;
struct wlc_compositor;

bool wlc_drm_init(struct wlc_backend *out_backend, struct wlc_compositor *compositor);

#endif /* _WLC_DRM_H_ */
