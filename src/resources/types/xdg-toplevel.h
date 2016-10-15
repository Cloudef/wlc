#ifndef _WLC_XDG_TOPLEVEL_H_
#define _WLC_XDG_TOPLEVEL_H_

#include <wayland-server.h>
#include "wayland-xdg-shell-unstable-v6-server-protocol.h"

const struct zxdg_toplevel_v6_interface* wlc_xdg_toplevel_implementation(void);

#endif /* _WLC_XDG_TOPLEVEL_H_ */
