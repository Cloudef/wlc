#ifndef _WLC_DEFINES_H_
#define _WLC_DEFINES_H_

#include <inttypes.h>

#if __GNUC__
#  define WLC_NONULL __attribute__((nonnull))
#  define WLC_NONULLV(...) __attribute__((nonnull(__VA_ARGS__)))
#else
#  define WLC_NONULL
#  define WLC_NONULLV
#endif

/** printf format specifiers. */
#define PRIoWLC PRIoPTR
#define PRIuWLC PRIuPTR
#define PRIxWLC PRIxPTR
#define PRIXWLC PRIXPTR

typedef uintptr_t wlc_handle;

#endif /* _WLC_DEFINES_H_ */
