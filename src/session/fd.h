#ifndef _WLC_FD_H_
#define _WLC_FD_H_

#include <stdbool.h>

enum wlc_fd_type {
   WLC_FD_INPUT,
   WLC_FD_LAST
};

bool wlc_fd_activate(void);
bool wlc_fd_deactivate(void);
int wlc_fd_open(const char *path, const int flags, const enum wlc_fd_type type);
void wlc_fd_close(const int fd);
void wlc_fd_init(const int argc, char *argv[]);

#endif /* _WLC_FD_H_ */
