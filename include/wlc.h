#ifndef _WLC_H_
#define _WLC_H_

struct wlc_compositor;

void wlc_compositor_run(struct wlc_compositor *compositor);
void wlc_compositor_free(struct wlc_compositor *compositor);
struct wlc_compositor* wlc_compositor_new(void);

#endif /* _WLC_H_ */
