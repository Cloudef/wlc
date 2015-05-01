#ifndef _WLC_FD_H_
#define _WLC_FD_H_

#include <stdbool.h>

enum wlc_fd_type {
   WLC_FD_INPUT,
   WLC_FD_DRM,
   WLC_FD_LAST
};

// Use these functions to control tty.
// Reason is that for vt's which we don't have session on, we need root permissions to switch.
bool wlc_fd_activate(void);
bool wlc_fd_deactivate(void);
bool wlc_fd_activate_vt(int vt);

int wlc_fd_open(const char *path, int flags, enum wlc_fd_type type);
void wlc_fd_close(int fd);
void wlc_fd_terminate(void);
void wlc_fd_init(int argc, char *argv[], bool has_logind);

#endif /* _WLC_FD_H_ */
