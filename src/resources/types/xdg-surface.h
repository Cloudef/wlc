#ifndef _WLC_XDG_SURFACE_H_
#define _WLC_XDG_SURFACE_H_

#include <wayland-server.h>
#include "wayland-xdg-shell-server-protocol.h"

const struct xdg_surface_interface* wlc_xdg_surface_implementation(void);

#endif /* _WLC_XDG_SURFACE_H_ */
