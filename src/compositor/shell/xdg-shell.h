#ifndef _WLC_XDG_SHELL_H_
#define _WLC_XDG_SHELL_H_

#include "resources/resources.h"

struct wlc_xdg_shell {
   struct wlc_source surfaces, toplevels, popups, positioners;

   struct {
      struct wl_global *xdg_shell;
   } wl;
};

void wlc_xdg_shell_release(struct wlc_xdg_shell *xdg_shell);
WLC_NONULL bool wlc_xdg_shell(struct wlc_xdg_shell *xdg_shell);

#endif /* _WLC_XDG_SHELL_H_ */
