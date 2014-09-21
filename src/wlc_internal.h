#ifndef _WLC_INTERNAL_H_
#define _WLC_INTERNAL_H_

#include <stdbool.h>

bool wlc_has_init(void);
int wlc_fd_open(const char *path, const int flags);
void wlc_fd_close(const int fd);

#endif /* _WLC_INTERNAL_H_ */
