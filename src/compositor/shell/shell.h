#ifndef _WLC_SHELL_H_
#define _WLC_SHELL_H_

#include "resources/resources.h"

struct wlc_shell {
   struct wlc_source surfaces;

   struct {
      struct wl_global *shell;
   } wl;
};

void wlc_shell_release(struct wlc_shell *shell);
bool wlc_shell(struct wlc_shell *shell);

#endif /* _WLC_SHELL_H_ */
