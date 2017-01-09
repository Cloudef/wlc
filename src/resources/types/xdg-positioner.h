#ifndef _WLC_XDG_POSITIONER_H_
#define _WLC_XDG_POSITIONER_H_

#include <wayland-server.h>
#include <wlc/wlc.h>
#include <wlc/geometry.h>
#include "wayland-xdg-shell-unstable-v6-server-protocol.h"

const struct zxdg_positioner_v6_interface* wlc_xdg_positioner_implementation(void);

enum wlc_xdg_positioner_flags {
   WLC_XDG_POSITIONER_HAS_SIZE = 1<<1,
   WLC_XDG_POSITIONER_HAS_ANCHOR_RECT = 1<<2,
   // offset, anchor and rest has default values
};

struct wlc_xdg_positioner {
   enum wlc_xdg_positioner_flags flags;
   struct wlc_size size;
   struct wlc_point offset;
   struct wlc_geometry anchor_rect;
   enum wlc_positioner_anchor_bit anchor;
   enum wlc_positioner_gravity_bit gravity;
   enum wlc_positioner_constraint_adjustment_bit constraint_adjustment;
};

#endif /* _WLC_XDG_POSITIONER_H_ */
