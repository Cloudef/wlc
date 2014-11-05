#ifndef _WLC_INTERNAL_H_
#define _WLC_INTERNAL_H_

#include "wlc.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

enum wlc_debug {
   WLC_DBG_RENDER,
   WLC_DBG_FOCUS,
   WLC_DBG_XWM,
   WLC_DBG_LAST,
};

WLC_LOG_ATTR(1, 2) static inline void
die(const char *format, ...)
{
   va_list vargs;
   va_start(vargs, format);
   wlc_vlog(WLC_LOG_ERROR, format, vargs);
   va_end(vargs);
   exit(EXIT_FAILURE);
}

WLC_LOG_ATTR(2, 3) void wlc_dlog(enum wlc_debug dbg, const char *fmt, ...);

uint32_t wlc_get_time(struct timespec *out_ts);
void wlc_set_active(bool active);
bool wlc_get_active(void);
bool wlc_has_init(void);
void wlc_terminate(void);

#endif /* _WLC_INTERNAL_H_ */
