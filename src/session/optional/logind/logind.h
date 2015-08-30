#ifndef _WLC_LOGIND_H_
#define _WLC_LOGIND_H_

#if HAS_LOGIND

/** Use wlc_fd_open instead, it automatically calls this if logind is used. */
WLC_NONULL int wlc_logind_open(const char *path, int flags);

/** Use wlc_fd_close instead, it automatically calls this if logind is used. */
void wlc_logind_close(int fd);

/** Check if logind is available. */
bool wlc_logind_available(void);

void wlc_logind_terminate(void);
WLC_NONULL int wlc_logind_init(const char *seat_id);

#else

/** For convenience. */
static inline bool
wlc_logind_available(void)
{
   return false;
}

#endif

#endif /* _WLC_LOGIND_H_ */
