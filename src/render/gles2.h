#ifndef _WLC_GLES2_H_
#define _WLC_GLES2_H_

#include <stdbool.h>

struct wlc_context;
struct wlc_render;

bool wlc_gles2_init(struct wlc_context *context, struct wlc_render *out_render);

#endif /* _WLC_GLES2_H_ */
