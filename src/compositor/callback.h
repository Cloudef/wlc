#ifndef _WLC_CALLBACK_H_
#define _WLC_CALLBACK_H_

struct wl_resource;

struct wlc_callback {
   struct wl_resource *resource;
};

void wlc_callback_implement(struct wlc_callback *callback);
void wlc_callback_free(struct wlc_callback *callback);
struct wlc_callback* wlc_callback_new(struct wl_resource *resource);

#endif /* _WLC_CALLBACK_H_ */
