#ifndef _WLC_CUSTOM_SHELL_H_
#define _WLC_CUSTOM_SHELL_H_

#include "resources/resources.h"

struct wlc_custom_shell {
   struct wlc_source surfaces;
};

void wlc_custom_shell_release(struct wlc_custom_shell *custom_shell);
WLC_NONULL bool wlc_custom_shell(struct wlc_custom_shell *custom_shell);

#endif /* _WLC_CUSTOM_SHELL_H_ */
