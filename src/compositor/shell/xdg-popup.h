#ifndef _WLC_XDG_POPUP_H_
#define _WLC_XDG_POPUP_H_

#include <stdbool.h>

#include "types/string.h"
#include "types/geometry.h"

struct wl_resource;
struct wlc_view;

// Inherted by wlc_view
struct wlc_xdg_popup {
   struct wl_resource *resource;
};

void wlc_xdg_popup_implement(struct wlc_xdg_popup *xdg_popup, struct wlc_view *view, struct wl_resource *resource);
void wlc_xdg_popup_release(struct wlc_xdg_popup *xdg_popup);

#endif /* _WLC_XDG_POPUP_H_ */
