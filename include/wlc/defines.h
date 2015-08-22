#ifndef _WLC_DEFINES_H_
#define _WLC_DEFINES_H_

#include <inttypes.h>

#if __GNUC__
#  define WLC_NONULL __attribute__((nonnull))
#  define WLC_NONULLV(...) __attribute__((nonnull(__VA_ARGS__)))
#  define WLC_PURE __attribute__((pure))
#  define WLC_CONST __attribute__((const))
#else
#  define WLC_NONULL
#  define WLC_NONULLV
#  define WLC_PURE
#  define WLC_CONST
#endif

/** printf format specifiers. */
#define PRIoWLC PRIoPTR
#define PRIuWLC PRIuPTR
#define PRIxWLC PRIxPTR
#define PRIXWLC PRIXPTR

typedef uintptr_t wlc_handle;

#endif /* _WLC_DEFINES_H_ */
