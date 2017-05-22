#ifndef _WLC_TOUCH_H_
#define _WLC_TOUCH_H_

#include <stdbool.h>
#include "resources/resources.h"

enum wlc_touch_type;
struct wlc_point;

struct wlc_touch {
   struct wlc_source resources;
   wlc_handle focus;
};

WLC_NONULL void wlc_touch_touch(struct wlc_touch *touch, uint32_t time, enum wlc_touch_type type, int32_t slot, const struct wlc_point *pos);
void wlc_touch_release(struct wlc_touch *touch);
WLC_NONULL bool wlc_touch(struct wlc_touch *touch);
wlc_handle view_under_touch(struct wlc_touch *touch, const struct wlc_point *pos);

#endif /* _WLC_TOUCH_H_ */
