#ifndef _WLC_INTERNAL_H_
#define _WLC_INTERNAL_H_

#include "wlc.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

enum wlc_debug {
   WLC_DBG_RENDER,
   WLC_DBG_LAST,
};

enum wlc_fd_type {
   WLC_FD_INPUT,
   WLC_FD_LAST
};

uint32_t wlc_get_time(struct timespec *out_ts);
WLC_LOG_ATTR(2, 3) void wlc_dlog(enum wlc_debug dbg, const char *fmt, ...);
bool wlc_has_init(void);
bool wlc_is_active(void);
bool wlc_no_egl_clients(void);
int wlc_fake_outputs(void);
void wlc_set_drm_control_functions(bool (*set_master)(void), bool (*drop_master)(void));
bool wlc_activate_vt(const int vt);
int wlc_fd_open(const char *path, const int flags, const enum wlc_fd_type type);
void wlc_fd_close(const int fd);

#endif /* _WLC_INTERNAL_H_ */
